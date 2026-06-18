#include "PageAircraft.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../providers/AircraftProvider.h"
#include "../providers/AviationWxProvider.h"
#include "../services/LocationService.h"
#include "../services/Settings.h"
#include "../services/AirportDB.h"
#include <math.h>
#include <time.h>

static constexpr double D2R = 3.14159265358979323846 / 180.0;

// Dead-reckon a contact's radar position forward from its last fix, so blips keep
// moving smoothly between ADS-B updates. Works in the observer's local NM tangent
// plane (fine at radar ranges); returns the extrapolated range + bearing.
static void drPos(const Aircraft& a, uint32_t lastDataMs, float& distNm, float& brgDeg) {
  distNm = a.distNm; brgDeg = a.bearingDeg;
  if (a.onGround || a.gsKt < 5) return;                 // parked / slow: leave it put
  float dt = a.seenS + (millis() - lastDataMs) / 1000.0f;   // seconds since the reported fix
  if (dt < 0) dt = 0; else if (dt > 30) dt = 30;        // cap runaway extrapolation if feed stalls
  float north = a.distNm * cosf(a.bearingDeg * D2R);    // offset from observer (nm)
  float east  = a.distNm * sinf(a.bearingDeg * D2R);
  float step  = a.gsKt * dt / 3600.0f;                  // nm travelled along the track
  north += step * cosf(a.trackDeg * D2R);
  east  += step * sinf(a.trackDeg * D2R);
  distNm = sqrtf(north * north + east * east);
  brgDeg = atan2f(east, north) / D2R; if (brgDeg < 0) brgDeg += 360;
}

// Decode an emergency transponder code; nullptr if it's a routine squawk.
static const char* squawkAlert(const String& sq) {
  if (sq == "7700") return "EMERGENCY";
  if (sq == "7600") return "RADIO FAIL";
  if (sq == "7500") return "HIJACK";
  return nullptr;
}

// ADS-B emitter category code -> class: 0 unknown, 1 airliner, 2 GA, 3 heli, 4 mil.
static int catClass(const String& c) {
  if (c == "A3" || c == "A4" || c == "A5") return 1;          // large/heavy
  if (c == "A1" || c == "A2" || c == "B1" || c == "B2" || c == "B4") return 2;  // light/glider/ultralight
  if (c == "A7") return 3;                                    // rotorcraft
  if (c == "A6") return 4;                                    // high-performance (mil/fast)
  return 0;
}
static const char* kAltLabel[] = {"alt:all", "alt:<10k", "alt:10-25k", "alt:>25k"};
static const char* kCatLabel[] = {"cat:all", "cat:airline", "cat:GA", "cat:heli", "cat:mil"};

// Build _filt: indices into _ap.aircraft() passing the altitude + category filters.
void PageAircraft::rebuildFilt() {
  _filt.clear();
  const auto& list = _ap.aircraft();
  for (int i = 0; i < (int)list.size(); ++i) {
    const Aircraft& a = list[i];
    float alt = a.onGround ? 0.0f : a.altFt;
    if (_altF == 1 && !(alt < 10000)) continue;
    if (_altF == 2 && !(alt >= 10000 && alt < 25000)) continue;
    if (_altF == 3 && !(alt >= 25000)) continue;
    if (_catF != 0 && catClass(a.category) != _catF) continue;
    _filt.push_back(i);
  }
  if (_sel >= (int)_filt.size()) _sel = _filt.empty() ? 0 : (int)_filt.size() - 1;
}

void PageAircraft::onEnter(App& app) {
  _dirty = _needClear = true;
  _ap.setForeground(true);   // full-rate polling + an immediate refresh on entry
  applyCenter();             // sync provider centre with the selected chip, then poll
}

void PageAircraft::applyCenter() {
  if (_centerIcao.length()) {
    for (const auto& s : _wx.stations())
      if (s.icao == _centerIcao) { _ap.setCenter(s.lat, s.lon); _ap.poll(); return; }
    _centerIcao = "";        // selected airport no longer in range
  }
  _ap.clearCenter(); _ap.poll();
}

void PageAircraft::onExit(App& app) {
  _ap.setForeground(false);  // drop to the 60 s background cadence
}

bool PageAircraft::autoAdvance(App&) {
  rebuildFilt();                           // single view: tour the filtered contacts
  int n = (int)_filt.size();
  if (n <= 0) return true;                 // nothing to show -> let the rotation move on
  _sel = (_sel + 1) % n; _needClear = _dirty = true;
  return _sel == 0;                        // wrapped = full cycle
}

void PageAircraft::onData(App& app, ProviderId id) {
  if (id == ProviderId::Aircraft) {
    int n = (int)_ap.aircraft().size();
    if (_sel >= n) _sel = n - 1;
    bool empty = (n == 0);
    if (empty != _wasEmpty) { _needClear = true; _wasEmpty = empty; }  // message<->radar
    _lastDataMs = millis();                    // reset the dead-reckoning clock on fresh data
  }
  _dirty = true;
}

void PageAircraft::onTouch(App& app, int x, int y) {
  if (handleChipTap(app, x, y)) return;            // top centre-selector chips
  if (handleRadiusTap(app, x, y)) return;          // bottom-left range badge
  if (handleGroundTap(app, x, y)) return;          // ground-filter badge
  if (handleAltTap(app, x, y)) return;             // altitude-band filter chip
  if (handleCatTap(app, x, y)) return;             // category filter chip
  rebuildFilt();
  const auto& list = _ap.aircraft();
  int n = (int)_filt.size();
  if (n == 0) { _sel = -1; return; }
  // Tap on (near) a radar blip selects it.
  if (_rR > 0 && x < app.contentW() / 2) {
    int ty = y + app.contentY();                    // onTouch y is content-relative
    int best = -1, bestd2 = 15 * 15;
    for (int k = 0; k < n; ++k) {
      const Aircraft& a = list[_filt[k]];
      float aDist, aBrg; drPos(a, _lastDataMs, aDist, aBrg);
      float rr = aDist / _rMaxR * _rR; if (rr > _rR) rr = _rR;
      int ax = _rCx + (int)round(rr * sin(aBrg * D2R));
      int ay = _rCy - (int)round(rr * cos(aBrg * D2R));
      int d2 = (ax - x) * (ax - x) + (ay - ty) * (ay - ty);
      if (d2 < bestd2) { bestd2 = d2; best = k; }
    }
    if (best >= 0) { _sel = best; _needClear = _dirty = true; return; }
  }
  int third = app.contentW() / 3;
  if (x < third)          { _sel = (_sel <= 0 ? n - 1 : _sel - 1); _needClear = true; }
  else if (x > 2 * third) { _sel = (_sel + 1) % n;                 _needClear = true; }
  _dirty = true;
}

bool PageAircraft::handleAltTap(App& app, int x, int yRel) {
  if (x < 134 || x >= 215 || yRel < app.contentH() - 20) return false;
  _altF = (_altF + 1) % 4; _sel = 0; _dirty = _needClear = true; return true;
}

bool PageAircraft::handleCatTap(App& app, int x, int yRel) {
  if (x < 215 || yRel < app.contentH() - 20) return false;
  _catF = (_catF + 1) % 5; _sel = 0; _dirty = _needClear = true; return true;
}

void PageAircraft::drawFilterBadges(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW();
  int y = app.contentY() + app.contentH() - 16;
  g.setTextSize(1);
  g.setTextDatum(textdatum_t::middle_left);
  g.fillRect(134, y, 79, 14, gTheme.grid);
  g.setTextColor(_altF ? gTheme.ok : gTheme.fg, gTheme.grid);
  g.drawString(kAltLabel[_altF], 138, y + 7);
  g.fillRect(215, y, cw - 215 - 2, 14, gTheme.grid);
  g.setTextColor(_catF ? gTheme.ok : gTheme.fg, gTheme.grid);
  g.drawString(kCatLabel[_catF], 219, y + 7);
}

bool PageAircraft::handleRadiusTap(App& app, int x, int yRel) {
  if (x > 64 || yRel < app.contentH() - 20) return false;
  int r = (int)_settings.getInt("adsbRadiusNm", 50);
  int next = (r <= 10) ? 15 : (r <= 15) ? 25 : (r <= 25) ? 50 : 10;   // 10/15/25/50
  _settings.set("adsbRadiusNm", (long)next);
  _settings.save();
  _ap.poll();                                      // refetch with the new radius
  _dirty = _needClear = true;                      // ring label changed -> relayout
  return true;
}

bool PageAircraft::handleGroundTap(App& app, int x, int yRel) {
  if (x < 68 || x > 132 || yRel < app.contentH() - 20) return false;
  bool hide = _settings.getInt("adsbHideGround", 0) != 0;
  _settings.set("adsbHideGround", (long)(hide ? 0 : 1));
  _settings.save();
  _ap.poll();                                      // refetch / re-filter
  _dirty = _needClear = true;
  return true;
}

void PageAircraft::drawRadiusBadge(App& app) {
  auto& g = app.display().gfx();
  int y = app.contentY() + app.contentH() - 16;
  g.fillRect(4, y, 56, 14, gTheme.grid);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(gTheme.fg, gTheme.grid);
  g.setTextSize(1);
  g.drawString(String((int)_ap.radiusNm()) + " nm", 8, y + 7);
}

void PageAircraft::drawGroundBadge(App& app) {
  auto& g = app.display().gfx();
  int y = app.contentY() + app.contentH() - 16;
  bool hide = _ap.hideGround();
  g.fillRect(64, y, 64, 14, gTheme.grid);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(hide ? gTheme.dim : gTheme.fg, gTheme.grid);   // dim when filtered out
  g.setTextSize(1);
  g.drawString(hide ? "gnd: off" : "gnd: on", 68, y + 7);
}

void PageAircraft::tick(App& app, uint32_t nowMs) {
  if (!_dirty && nowMs - _lastDraw < 1000) return;
  _dirty = false;
  _lastDraw = nowMs;
  draw(app);
}

void PageAircraft::drawMessage(App& app, const char* msg, int topY) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), bottom = app.contentY() + app.contentH();
  g.fillRect(0, topY, cw, bottom - topY, gTheme.bg);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.setTextSize(1);
  g.drawString(msg, cw / 2, (topY + bottom) / 2);
}

// Centre-selector chip row (top): HOME + nearby airports. Tap to recentre the
// radar on that airport. Records hit-boxes for handleChipTap.
int PageAircraft::drawChips(App& app) {
  const auto& st = _wx.stations();
  _chipCount = 0;
  if (st.empty()) return 0;
  // Drop a stale selection (the chosen airport left the station list).
  if (_centerIcao.length()) {
    bool found = false;
    for (const auto& s : st) if (s.icao == _centerIcao) { found = true; break; }
    if (!found) { _centerIcao = ""; _ap.clearCenter(); }
  }
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY();
  const int h = 13, top = cy0 + 3;          // small gap below the status strip
  int x = 2;
  g.setTextSize(1);
  g.setTextDatum(textdatum_t::middle_left);
  auto chip = [&](const String& label, bool sel, const String& icao) -> bool {
    int w = (int)label.length() * 6 + 8;
    if (x + w > cw - 2 || _chipCount >= kMaxChips) return false;
    g.fillRect(x, top, w, h, sel ? gTheme.accent : gTheme.grid);
    g.setTextColor(sel ? gTheme.bg : gTheme.fg, sel ? gTheme.accent : gTheme.grid);
    g.drawString(label, x + 4, top + h / 2);
    _chipX[_chipCount] = x; _chipW[_chipCount] = w; _chipIcao[_chipCount] = icao; _chipCount++;
    x += w + 3;
    return true;
  };
  chip("HOME", _centerIcao.length() == 0, "");
  for (const auto& s : st) if (!chip(s.icao, _centerIcao == s.icao, s.icao)) break;
  return h + 5;                              // 3 top gap + chip + 1 bottom
}

bool PageAircraft::handleChipTap(App& app, int x, int yRel) {
  if (yRel >= 14) return false;                    // chip row is the top band
  for (int i = 0; i < _chipCount; ++i)
    if (x >= _chipX[i] && x < _chipX[i] + _chipW[i]) {
      _centerIcao = _chipIcao[i];
      applyCenter();
      _sel = 0; _needClear = _dirty = true;
      return true;
    }
  return false;
}

void PageAircraft::draw(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), ch = app.contentH(), cy0 = app.contentY();
  if (!_loc.active().valid) { drawMessage(app, "no location", cy0); return; }
  if (_needClear) { g.fillRect(0, cy0, cw, ch, gTheme.bg); _needClear = false; }

  int chipH = drawChips(app);                      // centre selector (top)
  int top = cy0 + chipH;

  const auto& list = _ap.aircraft();
  if (list.empty()) {
    drawMessage(app, _ap.status() == ProviderStatus::Error ? "feed unavailable"
                  : _ap.status() == ProviderStatus::Loading ? "scanning..."
                  : _ap.hideGround() ? "no airborne aircraft in range"
                  : "no aircraft in range", top);
    drawNearestAirport(app, 8, top + 22, true); // still useful with no traffic: what's tunable near you
    drawRadiusBadge(app);    // keep the badges tappable so the user can widen
    drawGroundBadge(app);    // range or re-enable ground traffic from here
    drawFilterBadges(app);
    return;
  }
  rebuildFilt();
  if (_filt.empty()) {
    drawMessage(app, "no aircraft match filters", cy0 + chipH);
    drawRadiusBadge(app); drawGroundBadge(app); drawFilterBadges(app);
    return;
  }

  // Emergency-squawk alert strip (full width, below the chips). The page clears
  // fully when the emergency state flips so a cleared strip leaves no residue.
  int emIdx = -1;
  for (int i = 0; i < (int)list.size(); ++i) if (squawkAlert(list[i].squawk)) { emIdx = i; break; }
  if ((emIdx >= 0) != _wasEmerg) { g.fillRect(0, cy0, cw, ch, gTheme.bg); _wasEmerg = (emIdx >= 0); }
  int alertH = 0;
  if (emIdx >= 0) {
    const Aircraft& e = list[emIdx];
    int ay0 = cy0 + chipH;
    g.fillRect(0, ay0, cw, 14, gTheme.warn);
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextColor(gTheme.bg, gTheme.warn);
    g.setTextSize(1);
    g.drawString(String("! ") + e.squawk + " " + squawkAlert(e.squawk) + ": " +
                 (e.flight.length() ? e.flight : e.hex) + "  " + (int)round(e.distNm) + "nm",
                 4, ay0 + 7);
    alertH = 15;
  }
  top += alertH;

  // Radar on the left. Clear just the circle's bbox each tick (blips move);
  // the info column on the right redraws in place (padded) so it stays stable.
  int size = min(ch - 8 - chipH - alertH, cw / 2 - 8);
  int R = size / 2 - 12;
  int cx = 8 + R + 8, cy = top + (ch - chipH - alertH) / 2;
  float maxR = _ap.radiusNm();
  _rCx = cx; _rCy = cy; _rR = R; _rMaxR = maxR;     // remember for tap-on-blip
  g.fillRect(cx - R - 4, cy - R - 10, 2 * R + 8, 2 * R + 20, gTheme.bg);

  g.drawCircle(cx, cy, R, gTheme.grid);              // outer ring = selected range
  // Fixed-distance reference rings (5 / 10 nm) when they fit inside the range, so
  // close-in traffic has a scale even at the 25/50 nm settings.
  g.setTextDatum(textdatum_t::middle_center);
  for (int nm : {5, 10}) {
    if (nm >= (int)maxR) continue;
    int rr = (int)(nm / maxR * R);
    g.drawCircle(cx, cy, rr, gTheme.dim);
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(String(nm), cx, cy - rr - 4);
  }
  g.drawFastHLine(cx - R, cy, 2 * R, gTheme.grid);
  g.drawFastVLine(cx, cy - R, 2 * R, gTheme.grid);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString("N", cx, cy - R - 6);
  g.drawString(String((int)maxR) + "nm", cx + R - 8, cy - 6);

  for (int k = 0; k < (int)_filt.size(); ++k) {
    const Aircraft& a = list[_filt[k]];
    bool sel = (k == _sel);
    float aDist, aBrg; drPos(a, _lastDataMs, aDist, aBrg);   // dead-reckoned position
    float rr = aDist / maxR * R; if (rr > R) rr = R;
    int ax = cx + (int)round(rr * sin(aBrg * D2R));
    int ay = cy - (int)round(rr * cos(aBrg * D2R));
    bool emerg = squawkAlert(a.squawk) != nullptr;
    Color c = emerg ? gTheme.warn : sel ? gTheme.ok : (a.onGround ? gTheme.dim : gTheme.accent);
    // Heading tick in the track direction.
    int tx = ax + (int)round(7 * sin(a.trackDeg * D2R));
    int ty = ay - (int)round(7 * cos(a.trackDeg * D2R));
    g.drawLine(ax, ay, tx, ty, c);
    g.fillCircle(ax, ay, sel ? 3 : 2, c);
    if (emerg) g.drawCircle(ax, ay, 6, gTheme.warn);  // ring an emergency contact
    if (sel) {                                        // label the selected blip
      String cs = a.flight.length() ? a.flight : a.hex;
      g.setTextDatum(textdatum_t::bottom_left);
      g.setTextColor(gTheme.ok, gTheme.bg);
      g.drawString(cs, ax + 4, ay - 2);
    }
  }

  // Info column.
  int ix = cw / 2 + 8, iy = top + 6;
  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(1);
  auto line = [&](const String& s, Color col) { g.setTextColor(col, gTheme.bg); g.drawString(padRight(s, 20), ix, iy); iy += 14; };
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.setTextSize(2); g.drawString("Aircraft", ix, iy); iy += 20;
  g.setTextSize(1);
  line(String(_filt.size()) + "/" + list.size() + " @" + (_centerIcao.length() ? _centerIcao : String("HOME"))
       + "  " + (_ap.local() ? "local" : "cloud"), gTheme.dim);
  if (_ap.status() == ProviderStatus::Stale && _ap.lastFetched()) {     // own line (was overflowing)
    int age = (int)(time(nullptr) - _ap.lastFetched());
    if (age > 0) line("stale " + String(age) + "s", gTheme.warn);
  }

  if (_sel >= 0 && _sel < (int)_filt.size()) {
    const Aircraft& a = list[_filt[_sel]];
    iy += 4;
    g.setTextColor(gTheme.ok, gTheme.bg);
    g.setTextSize(2);
    g.drawString(a.flight.length() ? a.flight : a.hex, ix, iy); iy += 20;
    g.setTextSize(1);
    line(String(_sel + 1) + "/" + _filt.size() + "  (tap edges)", gTheme.dim);
    if (a.type.length() || a.category.length())
      line(String("type ") + (a.type.length() ? a.type : a.category), gTheme.fg);
    line(a.onGround ? String("on ground") : String("alt ") + (int)a.altFt + " ft", gTheme.fg);
    line(String("gs ") + (int)a.gsKt + " kt  trk " + (int)a.trackDeg, gTheme.fg);
    line(String("dist ") + (int)round(a.distNm) + " nm  brg " + (int)round(a.bearingDeg), gTheme.fg);
    // Look angle from the observer: az = bearing, el from altitude over ground range.
    if (!a.onGround) {
      double distFt = a.distNm * 6076.115;
      double el = distFt > 1 ? atan2((double)a.altFt, distFt) / D2R : 90.0;
      line(String("look az ") + (int)round(a.bearingDeg) + "\xF7 el " + (int)round(el) + "\xF7", gTheme.accent);
    }
    if (a.squawk.length()) {
      const char* em = squawkAlert(a.squawk);
      line(String("squawk ") + a.squawk + (em ? String("  ") + em : String()), em ? gTheme.warn : gTheme.dim);
    }
  }

  if (_sel >= 0 && _sel < (int)_filt.size()) drawNearestAirport(app, ix, iy + 4, false); // 1 line under details
  else                                       drawNearestAirport(app, ix, iy + 6, true);  // full list
  drawRadiusBadge(app);
  drawGroundBadge(app);
  drawFilterBadges(app);
}

// Nearest airport + its likely frequencies (the ham/SDR headline). `full` lists every
// frequency (when the info column is free); otherwise a one-line summary. US dataset.
void PageAircraft::drawNearestAirport(App& app, int x, int y, bool full) {
  auto& g = app.display().gfx();
  const int ch = app.contentH(), cy0 = app.contentY();
  if (!_loc.active().valid || !_adb.ready()) return;
  const AirportDB::Nearest& ap = _adb.nearest(_loc.active().lat, _loc.active().lon);
  if (!ap.valid || ap.distNm >= 250) return;
  static const char* kDir[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  const char* dir = kDir[((int)round(ap.brgDeg / 45.0)) & 7];
  g.setTextDatum(textdatum_t::top_left);

  if (!full) {                                       // one-line summary (aircraft selected)
    char ln[48];
    if (ap.n) snprintf(ln, sizeof(ln), "apt %s %dnm %s  %s %.2f", ap.id, (int)round(ap.distNm), dir,
                       AirportDB::label(ap.type[0]), ap.f40[0] / 40.0f);
    else      snprintf(ln, sizeof(ln), "apt %s %dnm %s", ap.id, (int)round(ap.distNm), dir);
    g.setTextColor(gTheme.accent, gTheme.bg); g.drawString(ln, x, y);
    return;
  }
  char b[48];                                        // header + the full frequency list
  snprintf(b, sizeof(b), "apt %s  %dnm %s", ap.id, (int)round(ap.distNm), dir);
  g.setTextColor(gTheme.accent, gTheme.bg); g.drawString(b, x, y); y += 13;
  int yMax = cy0 + ch - 20;                          // stop above the bottom badges
  g.setTextColor(gTheme.fg, gTheme.bg);
  for (int i = 0; i < ap.n && y <= yMax; ++i) {
    char ln[24];
    snprintf(ln, sizeof(ln), "%-4s %.2f", AirportDB::label(ap.type[i]), ap.f40[i] / 40.0f);
    g.drawString(ln, x, y); y += 12;
  }
}
