#include "PageAircraft.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../providers/AircraftProvider.h"
#include "../providers/AviationWxProvider.h"
#include "../services/LocationService.h"
#include "../services/Settings.h"
#include <math.h>
#include <time.h>

static constexpr double D2R = 3.14159265358979323846 / 180.0;

// Decode an emergency transponder code; nullptr if it's a routine squawk.
static const char* squawkAlert(const String& sq) {
  if (sq == "7700") return "EMERGENCY";
  if (sq == "7600") return "RADIO FAIL";
  if (sq == "7500") return "HIJACK";
  return nullptr;
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
  int n = (int)_ap.aircraft().size();      // single view: tour the contacts
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
  }
  _dirty = true;
}

void PageAircraft::onTouch(App& app, int x, int y) {
  if (handleChipTap(app, x, y)) return;            // top centre-selector chips
  if (handleRadiusTap(app, x, y)) return;          // bottom-left range badge
  if (handleGroundTap(app, x, y)) return;          // ground-filter badge (right of it)
  const auto& list = _ap.aircraft();
  int n = (int)list.size();
  if (n == 0) { _sel = -1; return; }
  // Tap on (near) a radar blip selects it.
  if (_rR > 0 && x < app.contentW() / 2) {
    int ty = y + app.contentY();                    // onTouch y is content-relative
    int best = -1, bestd2 = 15 * 15;
    for (int i = 0; i < n; ++i) {
      float rr = list[i].distNm / _rMaxR * _rR; if (rr > _rR) rr = _rR;
      int ax = _rCx + (int)round(rr * sin(list[i].bearingDeg * D2R));
      int ay = _rCy - (int)round(rr * cos(list[i].bearingDeg * D2R));
      int d2 = (ax - x) * (ax - x) + (ay - ty) * (ay - ty);
      if (d2 < bestd2) { bestd2 = d2; best = i; }
    }
    if (best >= 0) { _sel = best; _needClear = _dirty = true; return; }
  }
  int third = app.contentW() / 3;
  if (x < third)          { _sel = (_sel <= 0 ? n - 1 : _sel - 1); _needClear = true; }
  else if (x > 2 * third) { _sel = (_sel + 1) % n;                 _needClear = true; }
  _dirty = true;
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
    drawRadiusBadge(app);    // keep the badges tappable so the user can widen
    drawGroundBadge(app);    // range or re-enable ground traffic from here
    return;
  }
  if (_sel >= (int)list.size()) _sel = list.size() - 1;

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

  g.drawCircle(cx, cy, R, gTheme.grid);
  g.drawCircle(cx, cy, R / 2, gTheme.grid);
  g.drawFastHLine(cx - R, cy, 2 * R, gTheme.grid);
  g.drawFastVLine(cx, cy - R, 2 * R, gTheme.grid);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.setTextDatum(textdatum_t::middle_center);
  g.drawString("N", cx, cy - R - 6);
  g.drawString(String((int)maxR) + "nm", cx + R - 8, cy - 6);

  for (int i = 0; i < (int)list.size(); ++i) {
    const Aircraft& a = list[i];
    float rr = a.distNm / maxR * R; if (rr > R) rr = R;
    int ax = cx + (int)round(rr * sin(a.bearingDeg * D2R));
    int ay = cy - (int)round(rr * cos(a.bearingDeg * D2R));
    bool emerg = squawkAlert(a.squawk) != nullptr;
    Color c = emerg ? gTheme.warn : (i == _sel) ? gTheme.ok : (a.onGround ? gTheme.dim : gTheme.accent);
    // Heading tick in the track direction.
    int tx = ax + (int)round(7 * sin(a.trackDeg * D2R));
    int ty = ay - (int)round(7 * cos(a.trackDeg * D2R));
    g.drawLine(ax, ay, tx, ty, c);
    g.fillCircle(ax, ay, (i == _sel) ? 3 : 2, c);
    if (emerg) g.drawCircle(ax, ay, 6, gTheme.warn);  // ring an emergency contact
    if (i == _sel) {                                  // label the selected blip
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
  line(String(list.size()) + " @" + (_centerIcao.length() ? _centerIcao : String("HOME"))
       + "  " + (_ap.local() ? "local" : "cloud"), gTheme.dim);
  if (_ap.status() == ProviderStatus::Stale && _ap.lastFetched()) {     // own line (was overflowing)
    int age = (int)(time(nullptr) - _ap.lastFetched());
    if (age > 0) line("stale " + String(age) + "s", gTheme.warn);
  }

  if (_sel >= 0 && _sel < (int)list.size()) {
    const Aircraft& a = list[_sel];
    iy += 4;
    g.setTextColor(gTheme.ok, gTheme.bg);
    g.setTextSize(2);
    g.drawString(a.flight.length() ? a.flight : a.hex, ix, iy); iy += 20;
    g.setTextSize(1);
    line(String(_sel + 1) + "/" + list.size() + "  (tap edges)", gTheme.dim);
    if (a.type.length() || a.category.length())
      line(String("type ") + (a.type.length() ? a.type : a.category), gTheme.fg);
    line(a.onGround ? String("on ground") : String("alt ") + (int)a.altFt + " ft", gTheme.fg);
    line(String("gs ") + (int)a.gsKt + " kt  trk " + (int)a.trackDeg, gTheme.fg);
    line(String("dist ") + (int)round(a.distNm) + " nm  brg " + (int)round(a.bearingDeg), gTheme.fg);
    if (a.squawk.length()) {
      const char* em = squawkAlert(a.squawk);
      line(String("squawk ") + a.squawk + (em ? String("  ") + em : String()), em ? gTheme.warn : gTheme.dim);
    }
  }

  drawRadiusBadge(app);
  drawGroundBadge(app);
}
