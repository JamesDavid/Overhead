#include "PageSatellites.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../providers/TleProvider.h"
#include "../providers/Transponders.h"
#include "../services/LocationService.h"
#include "../services/TimeService.h"
#include "../services/Settings.h"
#include "../assets/Coastline.h"
#include <math.h>
#include <time.h>

static constexpr double D2R = 3.14159265358979323846 / 180.0;

int PageSatellites::minEl() const { return (int)_settings.getInt("satMinEl", 10); }

void PageSatellites::onEnter(App& app) { rebuildOrder(); _dirty = _needClear = true; }

void PageSatellites::onData(App& app, ProviderId id) {
  if (id == ProviderId::Tle)            { rebuildOrder(); _needClear = true; }
  else if (id == ProviderId::Location)  { reloadSelected(); recomputeTrack(time(nullptr)); _needClear = true; }
  _dirty = true;
}

void PageSatellites::rebuildOrder() {
  const auto& sats = _tle.sats();
  String keep = (_sel >= 0 && _sel < (int)sats.size()) ? sats[_sel].name : String();

  _order.clear();
  if (_settings.getBool("satWatchlistOnly", true)) {
    JsonArray wl = _settings.doc()["watchlist"].as<JsonArray>();
    for (JsonVariant v : wl) {
      String pre = (const char*)(v | "");
      if (!pre.length()) continue;
      for (size_t i = 0; i < sats.size(); ++i)
        if (sats[i].name.startsWith(pre)) _order.push_back((int)i);
    }
  }
  if (_order.empty())                                   // filter off, or nothing matched
    for (size_t i = 0; i < sats.size(); ++i) _order.push_back((int)i);

  if (_order.empty()) { _orderPos = -1; _sel = -1; _loaded = false; return; }

  // Preserve the current bird if still present; else prefer ISS; else first.
  int pos = 0;
  for (size_t i = 0; i < _order.size(); ++i) {
    if (keep.length() && sats[_order[i]].name == keep) { pos = (int)i; break; }
    if (sats[_order[i]].name.startsWith("ISS"))         pos = (int)i;
  }
  selectPos(pos);
}

void PageSatellites::selectPos(int pos) {
  if (_order.empty()) return;
  _orderPos = (pos % (int)_order.size() + _order.size()) % _order.size();
  _sel = _order[_orderPos];
  reloadSelected();
  time_t now = time(nullptr);
  if (_loaded) { recomputePass(now); recomputeTrack(now); }
  _dirty = _needClear = true;
}

void PageSatellites::reloadSelected() {
  _loaded = false;
  const auto& sats = _tle.sats();
  const auto& loc = _loc.active();
  if (_sel < 0 || _sel >= (int)sats.size() || !loc.valid) return;
  _eng.setObserver(loc.lat, loc.lon, 0.0);
  _eng.loadTle(sats[_sel].name.c_str(), sats[_sel].line1.c_str(), sats[_sel].line2.c_str());
  _loaded = true;
}

void PageSatellites::recomputePass(time_t now) {
  if (!_loaded) { _pass = astro::SatPass{}; _graphEl.clear(); return; }
  _pass = _eng.nextPass(now, (double)minEl(), 60);
  recomputeGraph();
}

void PageSatellites::recomputeGraph() {
  _graphEl.clear();
  if (!_pass.valid || _pass.los <= _pass.aos) return;
  const int N = 40;
  long span = (long)_pass.los - (long)_pass.aos;
  for (int k = 0; k <= N; ++k)
    _graphEl.push_back((float)_eng.elevationAt(_pass.aos + (time_t)(span * k / N)));
}

void PageSatellites::recomputeTrack(time_t now) {
  _track.clear();
  if (!_loaded) return;
  const int N = 48;
  const int spanSec = 95 * 60;                 // ~one LEO period
  for (int k = 0; k <= N; ++k) {
    astro::SatEngine::SubPoint sp = _eng.subPoint(now + (time_t)((long)k * spanSec / N));
    _track.push_back({ (float)sp.latDeg, (float)sp.lonDeg });
  }
}

void PageSatellites::onTouch(App& app, int x, int y) {
  if (handleMinElTap(app, x, y)) return;          // small bottom-left badge
  if (_order.empty()) return;
  int third = app.contentW() / 3;
  if (x < third)          selectPos(_orderPos - 1);
  else if (x > 2 * third) selectPos(_orderPos + 1);
  else {                                          // centre: cycle Polar->Ground->Graph
    _view = (_view == View::Polar) ? View::Ground
          : (_view == View::Ground) ? View::Graph : View::Polar;
    _dirty = _needClear = true;
  }
}

bool PageSatellites::handleMinElTap(App& app, int x, int yRel) {
  // Bottom-left badge cycles the min-elevation filter (0/10/20/30/40).
  if (x > 80 || yRel < app.contentH() - 20) return false;
  int v = minEl();
  int next = (v >= 40) ? 0 : v + 10;
  _settings.set("satMinEl", (long)next);
  _settings.save();
  recomputePass(time(nullptr));
  _dirty = true;
  return true;
}

void PageSatellites::tick(App& app, uint32_t nowMs) {
  if (!_dirty && nowMs - _lastDraw < 1000) return;
  _dirty = false;
  _lastDraw = nowMs;
  draw(app);
}

void PageSatellites::drawMessage(App& app, const char* msg) {
  auto& g = app.display().gfx();
  g.fillRect(0, app.contentY(), app.contentW(), app.contentH(), gTheme.bg);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.setTextSize(1);
  g.drawString(msg, app.contentW() / 2, app.contentY() + app.contentH() / 2);
}

void PageSatellites::draw(App& app) {
  if (!_time.synced())       { drawMessage(app, "waiting for time sync..."); return; }
  if (_tle.sats().empty())   { drawMessage(app, _tle.status() == ProviderStatus::Error ? "TLE fetch failed" : "loading TLEs..."); return; }
  if (!_loc.active().valid)  { drawMessage(app, "no location"); return; }
  if (!_loaded)              { drawMessage(app, "no satellite selected"); return; }

  time_t now = time(nullptr);
  if (!_pass.valid || now > _pass.los) { recomputePass(now); _needClear = true; }
  astro::SatObservation o = _eng.observe(now);

  auto& g = app.display().gfx();
  if (_needClear) {                                // full clear only on structural change
    g.fillRect(0, app.contentY(), app.contentW(), app.contentH(), gTheme.bg);
    _needClear = false; _pdx = _pdy = -1;
  }
  if      (_view == View::Polar)  drawPolarView(app, o);
  else if (_view == View::Ground) drawGroundView(app, o);
  else                            drawGraphView(app, o);
  if (_view != View::Graph) drawMinElBadge(app);   // graph shows the threshold line instead
}

void PageSatellites::drawMinElBadge(App& app) {
  auto& g = app.display().gfx();
  int y = app.contentY() + app.contentH() - 16;
  g.fillRect(4, y, 72, 14, gTheme.grid);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(gTheme.fg, gTheme.grid);
  g.setTextSize(1);
  g.drawString(String("minEl ") + minEl(), 8, y + 7);
}

void PageSatellites::drawInfoColumn(App& app, int ix, int iy, const astro::SatObservation& o) {
  auto& g = app.display().gfx();
  time_t now = time(nullptr);
  const auto& sat = _tle.sats()[_sel];

  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.setTextSize(2); g.drawString(sat.name.substring(0, 13), ix, iy); iy += 20;
  g.setTextSize(1);
  // padRight so shorter values overwrite longer ones in place (no clear/flicker).
  auto line = [&](const String& s, Color c) { g.setTextColor(c, gTheme.bg); g.drawString(padRight(s, 22), ix, iy); iy += 14; };

  line(String(_orderPos + 1) + "/" + _order.size() +
       (_settings.getBool("satWatchlistOnly", true) ? "  watchlist" : "  all"), gTheme.dim);
  bool up = o.elDeg > 0;
  line(String("az ") + (int)round(o.azDeg) + "  el " + (int)round(o.elDeg), up ? gTheme.ok : gTheme.dim);
  line(String("range ") + (int)round(o.rangeKm) + " km", gTheme.fg);
  line(o.sunlit ? "sunlit" : "eclipsed", o.sunlit ? gTheme.warn : gTheme.dim);

  if (up) {
    line(String("PASS NOW max ") + (int)round(_pass.maxElDeg), gTheme.ok);
  } else if (_pass.valid) {
    long t = (long)_pass.aos - (long)now; if (t < 0) t = 0;
    char b[32];
    if (t >= 3600) snprintf(b, sizeof(b), "AOS T-%ldh%02ldm  max %d", t / 3600, (t % 3600) / 60, (int)round(_pass.maxElDeg));
    else           snprintf(b, sizeof(b), "AOS T-%02ld:%02ld  max %d", t / 60, t % 60, (int)round(_pass.maxElDeg));
    line(b, gTheme.fg);
    struct tm tm; time_t a = _pass.aos; localtime_r(&a, &tm);
    char lb[20]; strftime(lb, sizeof(lb), "at %H:%M local", &tm);
    line(lb, gTheme.dim);
  } else {
    line("no pass in window", gTheme.dim);
  }

  const Transponder* tr = findTransponder(sat.name);
  if (tr) {
    double obsDown = astro::SatEngine::dopplerHz(tr->downHz, o.rangeRateKmS);
    char d[40];
    snprintf(d, sizeof(d), "DL %.3f %+.1fk", tr->downHz / 1e6, (obsDown - tr->downHz) / 1000.0);
    line(d, gTheme.accent);
    snprintf(d, sizeof(d), "UL %.3f %s", tr->upHz / 1e6, tr->mode);
    line(d, gTheme.dim);
  }
}

void PageSatellites::drawPolarView(App& app, const astro::SatObservation& o) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), ch = app.contentH(), cy0 = app.contentY();

  int size = min(ch - 8, cw / 2 - 8);
  int R = size / 2 - 12;
  int cx = 8 + R + 8, cy = cy0 + ch / 2;

  if (_pdx >= 0) g.fillCircle(_pdx, _pdy, 4, gTheme.bg);   // erase old blip first

  // Static grid (redrawn in place each tick = stable, also restores any erase).
  g.drawCircle(cx, cy, R, gTheme.grid);
  g.drawCircle(cx, cy, R * 2 / 3, gTheme.grid);
  g.drawCircle(cx, cy, R / 3, gTheme.grid);
  g.drawFastHLine(cx - R, cy, 2 * R, gTheme.grid);
  g.drawFastVLine(cx, cy - R, 2 * R, gTheme.grid);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.setTextDatum(textdatum_t::middle_center);
  g.drawString("N", cx, cy - R - 6); g.drawString("S", cx, cy + R + 6);
  g.drawString("E", cx + R + 6, cy); g.drawString("W", cx - R - 6, cy);

  if (o.elDeg > 0) {
    double rr = R * (90.0 - o.elDeg) / 90.0;
    int sx = cx + (int)round(rr * sin(o.azDeg * D2R));
    int sy = cy - (int)round(rr * cos(o.azDeg * D2R));
    g.fillCircle(sx, sy, 4, o.sunlit ? gTheme.warn : gTheme.accent);
    _pdx = sx; _pdy = sy;
  } else {
    _pdx = -1;
  }

  drawInfoColumn(app, cw / 2 + 8, cy0 + 6, o);
}

void PageSatellites::drawGroundView(App& app, const astro::SatObservation& o) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  const int topH = 28;
  const int mx = 0, my = cy0 + topH, mw = cw, mh = ch - topH;

  auto px = [&](double lon) { return mx + (int)round((lon + 180.0) / 360.0 * mw); };
  auto py = [&](double lat) { return my + (int)round((90.0 - lat) / 180.0 * mh); };

  if (_pdx >= 0) g.fillCircle(_pdx, _pdy, 3, gTheme.bg);   // erase old sat point

  // Map frame + 30-degree graticule (redrawn in place = stable).
  g.drawRect(mx, my, mw, mh, gTheme.grid);
  for (int lon = -150; lon < 180; lon += 30) g.drawFastVLine(px(lon), my, mh, gTheme.grid);
  for (int lat = -60; lat < 90;  lat += 30) g.drawFastHLine(mx, py(lat), mw, gTheme.grid);
  g.drawFastHLine(mx, py(0), mw, gTheme.dim);   // equator

  // Coastline (coarse; see assets/Coastline.h).
  for (int i = 1; i < kCoastlineCount; ++i) {
    const CoastPt& a = kCoastline[i - 1];
    const CoastPt& b = kCoastline[i];
    if (a.lon == 999 || b.lon == 999) continue;          // pen-up separator
    if (abs(a.lon - b.lon) > 180) continue;              // seam
    g.drawLine(px(a.lon), py(a.lat), px(b.lon), py(b.lat), gTheme.dim);
  }

  // Ground track (skip the +/-180 seam).
  for (size_t i = 1; i < _track.size(); ++i) {
    if (fabs(_track[i].lon - _track[i - 1].lon) > 180.0f) continue;
    g.drawLine(px(_track[i - 1].lon), py(_track[i - 1].lat),
               px(_track[i].lon),     py(_track[i].lat), gTheme.accent);
  }

  // Observer + current sub-satellite point.
  const auto& loc = _loc.active();
  int oxv = px(loc.lon), oyv = py(loc.lat);
  g.drawFastHLine(oxv - 3, oyv, 7, gTheme.ok);
  g.drawFastVLine(oxv, oyv - 3, 7, gTheme.ok);
  int spx = px(o.subLonDeg), spy = py(o.subLatDeg);
  g.fillCircle(spx, spy, 3, o.sunlit ? gTheme.warn : gTheme.fg);
  _pdx = spx; _pdy = spy;

  // Compact info line on top.
  const auto& sat = _tle.sats()[_sel];
  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(1);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString(sat.name.substring(0, 16), 4, cy0 + 3);
  g.setTextColor(o.elDeg > 0 ? gTheme.ok : gTheme.dim, gTheme.bg);
  char b[40];
  snprintf(b, sizeof(b), "el %d  %s", (int)round(o.elDeg), o.sunlit ? "sun" : "ecl");
  g.setTextDatum(textdatum_t::top_right);
  g.drawString(b, cw - 4, cy0 + 3);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.setTextDatum(textdatum_t::top_left);
  g.drawString("centre: next view  |  edges: select", 4, cy0 + 15);
}

void PageSatellites::drawGraphView(App& app, const astro::SatObservation& o) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  const auto& sat = _tle.sats()[_sel];

  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(1);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString(sat.name.substring(0, 16) + " - pass elevation", 4, cy0 + 3);

  if (!_pass.valid || _graphEl.size() < 2) {
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(String("no pass >= ") + minEl() + " deg in window", 4, cy0 + ch / 2);
    return;
  }

  // Plot area.
  const int gx = 30, gy = cy0 + 18, gw = cw - gx - 8, gh = ch - 18 - 22;
  g.drawRect(gx, gy, gw, gh, gTheme.grid);
  // Y gridlines at 30/60/90 deg.
  for (int e = 30; e <= 90; e += 30) {
    int yy = gy + gh - (int)((float)e / 90.0f * gh);
    g.drawFastHLine(gx, yy, gw, gTheme.grid);
    g.setTextDatum(textdatum_t::middle_right);
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(String(e), gx - 3, yy);
  }
  // min-el threshold line.
  int yMin = gy + gh - (int)((float)minEl() / 90.0f * gh);
  g.drawFastHLine(gx, yMin, gw, gTheme.warn);

  // Elevation curve.
  int prevx = 0, prevy = 0;
  for (size_t i = 0; i < _graphEl.size(); ++i) {
    float el = _graphEl[i] < 0 ? 0 : _graphEl[i];
    int xx = gx + (int)((float)i / (_graphEl.size() - 1) * gw);
    int yy = gy + gh - (int)(el / 90.0f * gh);
    if (i) g.drawLine(prevx, prevy, xx, yy, gTheme.accent);
    prevx = xx; prevy = yy;
  }

  // "now" marker if the pass is in progress.
  time_t now = time(nullptr);
  if (now >= _pass.aos && now <= _pass.los) {
    int xx = gx + (int)((float)(now - _pass.aos) / (float)(_pass.los - _pass.aos) * gw);
    g.drawFastVLine(xx, gy, gh, gTheme.ok);
  }

  // Labels.
  struct tm tm; time_t a = _pass.aos; localtime_r(&a, &tm);
  char b[40];
  snprintf(b, sizeof(b), "AOS %02d:%02d  max %d deg  dur %lus",
           tm.tm_hour, tm.tm_min, (int)round(_pass.maxElDeg), (unsigned long)_pass.durationSec);
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.drawString(b, gx, gy + gh + 4);
}
