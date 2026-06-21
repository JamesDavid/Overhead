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

void PageSatellites::cycleView(int) {
  _view = (_view == View::Polar) ? View::Ground : View::Polar;
  _dirty = _needClear = true;
}

void PageSatellites::onEnter(App& app) {
  rebuildOrder();
  String f = app.takeFocus();                  // Agenda tap -> focus the exact bird
  if (f.length()) focusBird(f);
  _dirty = _needClear = true;
}

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
        if (satNameMatches(sats[i].name, pre)) _order.push_back((int)i);
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

void PageSatellites::focusBird(const String& namePrefix) {
  const auto& sats = _tle.sats();
  for (size_t i = 0; i < _order.size(); ++i)
    if (satNameMatches(sats[_order[i]].name, namePrefix)) { selectPos((int)i); return; }
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
  if (!_loaded) { _pass = astro::SatPass{}; _graphEl.clear(); _passAz.clear(); return; }
  _pass = _eng.nextPass(now, (double)minEl(), 60);
  recomputeGraph();
}

void PageSatellites::recomputeGraph() {
  _graphEl.clear(); _passAz.clear();
  if (!_pass.valid || _pass.los <= _pass.aos) return;
  // Sample horizon-to-horizon (extend past the minEl crossings down to el<=0) so the
  // polar view can dash the below-threshold approach/departure.
  time_t rise = _pass.aos, set = _pass.los;
  for (int g = 0; g < 40 && _eng.observe(rise).elDeg > 0.0; ++g) rise -= 30;
  for (int g = 0; g < 40 && _eng.observe(set).elDeg  > 0.0; ++g) set  += 30;
  const int N = 56;
  long span = (long)set - (long)rise;
  if (span <= 0) return;
  for (int k = 0; k <= N; ++k) {                       // az + el across the full pass
    astro::SatObservation o = _eng.observe(rise + (time_t)(span * k / N));
    _graphEl.push_back((float)o.elDeg);
    _passAz.push_back((float)o.azDeg);
  }
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

bool PageSatellites::autoAdvance(App&) {
  if (_order.empty()) return true;                // nothing to tour -> let rotation move on
  bool cycled = false;
  if (_orderPos + 1 >= (int)_order.size()) {      // toured all birds -> next view, restart
    bool wasGround = (_view == View::Ground);
    _view = (_view == View::Polar) ? View::Ground : View::Polar;
    selectPos(0);
    if (wasGround) cycled = true;                 // Ground -> Polar = full cycle
  } else {
    selectPos(_orderPos + 1);
  }
  _needClear = _dirty = true;
  return cycled;
}

void PageSatellites::onTouch(App& app, int x, int y) {
  if (handleMinElTap(app, x, y)) return;          // small bottom-left badge
  if (handleChipTap(app, x, y)) return;           // tracked-sat selector chips (top band)
  if (_order.empty()) return;
  int third = app.contentW() / 3;
  if (x < third)          selectPos(_orderPos - 1);
  else if (x > 2 * third) selectPos(_orderPos + 1);
  else {                                          // centre: cycle Polar <-> Ground
    _view = (_view == View::Polar) ? View::Ground : View::Polar;
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

String PageSatellites::gridStatus() {
  // The soonest AOS across ALL tracked birds (not just the selected one), named.
  const auto& sats = _tle.sats();
  const auto& loc = _loc.active();
  if (sats.empty() || !loc.valid) return String();
  // Use the (watchlist-filtered) order if it's been built; otherwise scan every
  // loaded bird so the tile works even before the page has been opened.
  std::vector<int> all;
  const std::vector<int>* scan = &_order;
  if (_order.empty()) { for (int i = 0; i < (int)sats.size(); ++i) all.push_back(i); scan = &all; }
  time_t now = time(nullptr);
  _eng.setObserver(loc.lat, loc.lon, 0.0);
  bool have = false, bestNow = false; time_t bestAos = 0; int bestIdx = -1;
  for (int idx : *scan) {
    if (idx < 0 || idx >= (int)sats.size()) continue;
    if (!_eng.loadTle(sats[idx].name.c_str(), sats[idx].line1.c_str(), sats[idx].line2.c_str())) continue;
    astro::SatPass p = _eng.nextPass(now, (double)minEl(), 60);
    if (!p.valid) continue;
    if (!have || p.aos < bestAos) { have = true; bestAos = p.aos; bestIdx = idx; bestNow = (now >= p.aos && now <= p.los); }
  }
  reloadSelected();                          // restore the engine to the selected bird
  if (!have) return String();
  String nm = sats[bestIdx].name;            // short name: drop the "(...)" designator
  int paren = nm.indexOf('('); if (paren > 0) nm = nm.substring(0, paren);
  nm.trim(); if (nm.length() > 9) nm = nm.substring(0, 9);
  if (bestNow) return nm + " now";
  long s = (long)bestAos - (long)now; if (s < 0) s = 0;
  return nm + " " + (s >= 3600 ? String(s / 3600) + "h" : String(s / 60) + "m");
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
  drawChips(app);                                  // tracked-sat chips (sets the top band)
  if (_view == View::Polar) drawPolarView(app, o);
  else                      drawGroundView(app, o);
  drawMinElBadge(app);
  g.setTextDatum(textdatum_t::bottom_right);       // consistent control hint across views
  g.setTextColor(gTheme.dim, gTheme.bg); g.setTextSize(1);
  g.drawString("[side tap: sat  mid: view]", app.contentW() - 4, app.contentY() + app.contentH() - 1);
}

// Tracked-satellite selector chips along the top (same shared chip row the Aircraft
// page uses for airports). Tap a chip to jump to that bird; reserves a top band the
// views offset below. Hidden when there's 0/1 bird (nothing to pick).
void PageSatellites::drawChips(App& app) {
  _chipN = 0; _chipBandH = 0;
  int n = (int)_order.size();
  if (n <= 1) return;
  const auto& sats = _tle.sats();
  String labels[kMaxChips]; int m = 0, sel = 0;
  for (int i = 0; i < n && m < kMaxChips; ++i) {
    String nm = sats[_order[i]].name;
    int lp = nm.indexOf('('), rp = nm.indexOf(')');
    if (lp >= 0 && rp > lp) nm = nm.substring(lp + 1, rp);          // prefer the (designator), e.g. SO-50
    else { int sp = nm.indexOf(' '); if (sp > 0) nm = nm.substring(0, sp); }
    if (i == _orderPos) sel = m;
    labels[m++] = nm;
  }
  _chipN = app.drawChipRow(2, app.contentY() + 3, 13, labels, m, sel, _chipX, _chipW, kMaxChips);
  _chipBandH = 18;
}

bool PageSatellites::handleChipTap(App& app, int x, int yRel) {
  if (_chipBandH == 0 || yRel >= _chipBandH) return false;
  for (int i = 0; i < _chipN; ++i)
    if (x >= _chipX[i] && x < _chipX[i] + _chipW[i]) { selectPos(i); _needClear = _dirty = true; return true; }
  return false;
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

  auto hm = [](time_t t) { struct tm tm; localtime_r(&t, &tm); char b[8]; snprintf(b, sizeof(b), "%02d:%02d", tm.tm_hour, tm.tm_min); return String(b); };
  if (up) {
    line(String("PASS NOW max ") + (int)round(_pass.maxElDeg), gTheme.ok);
    if (_pass.valid) line("LOS " + hm(_pass.los) + " (ends)", gTheme.dim);
  } else if (_pass.valid) {
    long t = (long)_pass.aos - (long)now; if (t < 0) t = 0;
    char b[32];
    if (t >= 3600) snprintf(b, sizeof(b), "AOS T-%ldh%02ldm  max %d", t / 3600, (t % 3600) / 60, (int)round(_pass.maxElDeg));
    else           snprintf(b, sizeof(b), "AOS T-%02ld:%02ld  max %d", t / 60, t % 60, (int)round(_pass.maxElDeg));
    line(b, gTheme.fg);
    line("AOS " + hm(_pass.aos) + "  LOS " + hm(_pass.los), gTheme.dim);
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
  const int cw = app.contentW(), ch = app.contentH() - _chipBandH, cy0 = app.contentY() + _chipBandH;

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

  // Predicted pass arc: the az/el trajectory the satellite traces from AOS to LOS.
  auto polar = [&](float az, float el, int& sx, int& sy) {
    double rr = R * (90.0 - el) / 90.0;
    sx = cx + (int)round(rr * sin(az * D2R));
    sy = cy - (int)round(rr * cos(az * D2R));
  };
  if (_passAz.size() >= 2 && _passAz.size() == _graphEl.size()) {
    int me = minEl();
    int px = -1, py = -1; float pel = -1; int aosIdx = -1, losIdx = -1;
    for (size_t i = 0; i < _passAz.size(); ++i) {
      float el = _graphEl[i];
      if (el <= 0) { px = -1; pel = el; continue; }              // below horizon: gap
      if (el >= me) { if (aosIdx < 0) aosIdx = (int)i; losIdx = (int)i; }
      int sx, sy; polar(_passAz[i], el, sx, sy);
      if (px >= 0) {
        if (el >= me && pel >= me) g.drawLine(px, py, sx, sy, gTheme.accent);   // above minEl: solid
        else if (i & 1)            g.drawLine(px, py, sx, sy, gTheme.dim);       // below minEl: dashed
      }
      px = sx; py = sy; pel = el;
    }
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextColor(gTheme.dim, gTheme.bg);
    if (aosIdx >= 0) { int sx, sy; polar(_passAz[aosIdx], _graphEl[aosIdx], sx, sy); g.fillCircle(sx, sy, 2, gTheme.ok); g.drawString("AOS", sx + 4, sy); }
    if (losIdx >= 0) { int sx, sy; polar(_passAz[losIdx], _graphEl[losIdx], sx, sy); g.fillCircle(sx, sy, 2, gTheme.ok); g.drawString("LOS", sx + 4, sy); }
  }

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
  const int cw = app.contentW(), cy0 = app.contentY() + _chipBandH, ch = app.contentH() - _chipBandH;
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

  // Coastlines + borders (Natural Earth; coords in 0.1-degree units, see Coastline.h).
  for (int i = 1; i < kCoastlineCount; ++i) {
    const CoastPt& a = kCoastline[i - 1];
    const CoastPt& b = kCoastline[i];
    if (a.lon == 9999 || b.lon == 9999) continue;        // pen-up separator
    if (abs(a.lon - b.lon) > 1800) continue;             // seam
    g.drawLine(px(a.lon / 10.0), py(a.lat / 10.0), px(b.lon / 10.0), py(b.lat / 10.0), gTheme.dim);
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
  g.fillCircle(oxv, oyv, 4, gTheme.ok);          // observer: green circle + black centre dot
  g.fillCircle(oxv, oyv, 1, 0x0000);
  int spx = px(o.subLonDeg), spy = py(o.subLatDeg);
  g.fillCircle(spx, spy, 3, o.sunlit ? gTheme.warn : gTheme.fg);
  _pdx = spx; _pdy = spy;

  // Compact info line on top.
  const auto& sat = _tle.sats()[_sel];
  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(1);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString(sat.name.substring(0, 16), 4, cy0 + 3);
  if (_pass.valid) {                               // next AOS / max-el / LOS under the name
    auto hm2 = [](time_t t){ struct tm tm; localtime_r(&t,&tm); char b[8]; snprintf(b,sizeof(b),"%02d:%02d",tm.tm_hour,tm.tm_min); return String(b); };
    long dt = (long)_pass.aos - (long)time(nullptr);
    char pl[52];
    if (dt > 0) snprintf(pl, sizeof(pl), "AOS %s  max %d\xF7  LOS %s", hm2(_pass.aos).c_str(), (int)round(_pass.maxElDeg), hm2(_pass.los).c_str());
    else        snprintf(pl, sizeof(pl), "NOW  max %d\xF7  LOS %s", (int)round(_pass.maxElDeg), hm2(_pass.los).c_str());
    g.setTextDatum(textdatum_t::top_left); g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(pl, 4, cy0 + 15);
  }
  g.setTextDatum(textdatum_t::top_right);
  g.setTextColor(o.elDeg > 0 ? gTheme.ok : gTheme.dim, gTheme.bg);
  char b[40];
  snprintf(b, sizeof(b), "el %d  %s", (int)round(o.elDeg), o.sunlit ? "sun" : "ecl");
  g.setTextDatum(textdatum_t::top_right);
  g.drawString(b, cw - 4, cy0 + 3);
}

