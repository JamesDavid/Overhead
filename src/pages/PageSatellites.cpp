#include "PageSatellites.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../providers/TleProvider.h"
#include "../providers/Transponders.h"
#include "../services/LocationService.h"
#include "../services/TimeService.h"
#include "../services/Settings.h"
#include <math.h>
#include <time.h>

static constexpr double D2R = 3.14159265358979323846 / 180.0;

void PageSatellites::onEnter(App& app) { rebuildOrder(); _dirty = true; }

void PageSatellites::onData(App& app, ProviderId id) {
  if (id == ProviderId::Tle)            rebuildOrder();
  else if (id == ProviderId::Location)  { reloadSelected(); recomputeTrack(time(nullptr)); }
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
  _dirty = true;
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
  if (_loaded) _pass = _eng.nextPass(now, 0.0, 60);
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
  if (_order.empty()) return;
  int third = app.contentW() / 3;
  if (x < third)          selectPos(_orderPos - 1);
  else if (x > 2 * third) selectPos(_orderPos + 1);
  else { _view = (_view == View::Polar) ? View::Ground : View::Polar; _dirty = true; }
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
  if (!_pass.valid || now > _pass.los) recomputePass(now);
  astro::SatObservation o = _eng.observe(now);

  auto& g = app.display().gfx();
  g.fillRect(0, app.contentY(), app.contentW(), app.contentH(), gTheme.bg);
  if (_view == View::Polar) drawPolarView(app, o);
  else                      drawGroundView(app, o);
}

void PageSatellites::drawInfoColumn(App& app, int ix, int iy, const astro::SatObservation& o) {
  auto& g = app.display().gfx();
  time_t now = time(nullptr);
  const auto& sat = _tle.sats()[_sel];

  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.setTextSize(2); g.drawString(sat.name.substring(0, 13), ix, iy); iy += 20;
  g.setTextSize(1);
  auto line = [&](const String& s, Color c) { g.setTextColor(c, gTheme.bg); g.drawString(s, ix, iy); iy += 14; };

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
    char b[28]; snprintf(b, sizeof(b), "AOS T-%02ld:%02ld max %d", t / 60, t % 60, (int)round(_pass.maxElDeg));
    line(b, gTheme.fg);
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

  // Map frame + 30-degree graticule.
  g.drawRect(mx, my, mw, mh, gTheme.grid);
  for (int lon = -150; lon < 180; lon += 30) g.drawFastVLine(px(lon), my, mh, gTheme.grid);
  for (int lat = -60; lat < 90;  lat += 30) g.drawFastHLine(mx, py(lat), mw, gTheme.grid);
  g.drawFastHLine(mx, py(0), mw, gTheme.dim);   // equator

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
  g.fillCircle(px(o.subLonDeg), py(o.subLatDeg), 3, o.sunlit ? gTheme.warn : gTheme.fg);

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
  g.drawString("tap centre: polar  |  edges: select", 4, cy0 + 15);
}
