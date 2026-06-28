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
static constexpr uint32_t kAutoCycleMs = 4000;   // dwell per contact while auto-cycling

// Dead-reckon a contact's radar position forward from its last fix, so blips keep
// moving smoothly between ADS-B updates. Works in the observer's local NM tangent
// plane (fine at radar ranges); returns the extrapolated range + bearing.
static void drPos(const Aircraft& a, uint32_t lastDataMs, float& distNm, float& brgDeg) {
  distNm = a.distNm; brgDeg = a.bearingDeg;
  if (a.onGround || a.gsKt < 5) return;                 // parked / slow: leave it put
  float dt = a.seenS + (millis() - lastDataMs) / 1000.0f;   // seconds since the reported fix
  if (dt < 0) dt = 0; else if (dt > 60) dt = 60;        // keep dead-reckoning a stale contact for its full
                                                        // tracked life (provider drops it at seen>60s), so it
                                                        // glides on instead of freezing at 30s then snapping
  float north = a.distNm * cosf(a.bearingDeg * D2R);    // offset from observer (nm)
  float east  = a.distNm * sinf(a.bearingDeg * D2R);
  float step  = a.gsKt * dt / 3600.0f;                  // nm travelled along the track
  north += step * cosf(a.trackDeg * D2R);
  east  += step * sinf(a.trackDeg * D2R);
  distNm = sqrtf(north * north + east * east);
  brgDeg = atan2f(east, north) / D2R; if (brgDeg < 0) brgDeg += 360;
}

// Dead-reckon a contact's altitude forward from its last fix using its reported vertical rate, so
// the altitude marker keeps tracking (e.g. a 700 fpm descent) between ADS-B updates — the vertical
// analogue of drPos(). Grounded contacts return their reported altitude unchanged.
static float drAlt(const Aircraft& a, uint32_t lastDataMs) {
  if (a.onGround) return a.altFt;
  float dt = a.seenS + (millis() - lastDataMs) / 1000.0f;   // seconds since the reported fix
  if (dt < 0) dt = 0; else if (dt > 60) dt = 60;            // bounded like drPos (provider drops at 60s)
  float alt = a.altFt + a.vsFpm * (dt / 60.0f);            // vsFpm = ft/min -> ft over dt seconds
  return alt < 0 ? 0.0f : alt;
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

// Local-tangent-plane distance (nm) between two lat/lon points — fine at radar/airport ranges.
static float distNmLL(double lat1, double lon1, double lat2, double lon2) {
  double mlat = (lat1 + lat2) * 0.5 * D2R;
  double dN = (lat2 - lat1) * 60.0;                  // 1 deg latitude  = 60 nm
  double dE = (lon2 - lon1) * 60.0 * cos(mlat);      // 1 deg longitude = 60 nm * cos(lat)
  return (float)sqrt(dN * dN + dE * dE);
}
static String actLabel(PageAircraft::Activity act, const String& apt) {
  switch (act) {
    case PageAircraft::ActGround:    return "on ground";
    case PageAircraft::ActDeparting: return "DEP " + apt;
    case PageAircraft::ActArriving:  return "ARR " + apt;
    case PageAircraft::ActLocal:     return "local " + apt;
    case PageAircraft::ActTransit:   return "transit";
    default:                         return "?";
  }
}
static Color actColor(PageAircraft::Activity act) {
  switch (act) {
    case PageAircraft::ActDeparting: return gTheme.ok;       // climbing out
    case PageAircraft::ActArriving:  return gTheme.warn;     // descending in
    case PageAircraft::ActLocal:     return gTheme.accent;   // in the pattern
    case PageAircraft::ActGround:    return gTheme.dim;
    default:                         return gTheme.fg;       // transit / en-route
  }
}

// Infer what a contact is doing from altitude + vertical rate + the nearest known field.
// METAR stations are already loaded in RAM (sorted by range), so this is cheap — no file scan.
PageAircraft::Activity PageAircraft::classify(const Aircraft& a, String& aptOut) const {
  aptOut = "";
  if (a.onGround) return ActGround;
  float best = 1e9f; String bestId;
  for (const auto& s : _wx.stations()) {
    float d = distNmLL(a.lat, a.lon, s.lat, s.lon);
    if (d < best) { best = d; bestId = s.icao; }
  }
  if (bestId.length() && best < 12.0f && drAlt(a, _lastDataMs) < 11000.0f) {  // inside a field's terminal area, low
    aptOut = bestId;
    if (a.vsFpm < -300) return ActArriving;
    if (a.vsFpm >  300) return ActDeparting;
    return ActLocal;                                            // low + level near a field (pattern/approach)
  }
  return ActTransit;                                            // en-route / high / far from any field
}

const char* PageAircraft::viewName(int i) const {
  static const char* nm[] = {"Radar", "Activity", "Stats"};
  return (i >= 0 && i < 3) ? nm[i] : nullptr;
}
void PageAircraft::cycleView(int dir) {
  _view = (_view + dir + 3) % 3;
  _userSel = false; _sel = -1;                                  // each view restarts its own selection
  _needClear = _dirty = true;
}

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

bool PageAircraft::autoSkip() {
  // Skip in the AUTO tour when the feed is unavailable AND there's nothing to show.
  return _ap.aircraft().empty() && _ap.status() == ProviderStatus::Error;
}

String PageAircraft::gridStatus() {
  int n = (int)_ap.aircraft().size();
  if (n) return String(n) + " ac";
  if (_ap.status() == ProviderStatus::Error) {                     // ADS-B source unavailable
    if (Display::largestFreeBlock() < 42000) return "feed down\nmem low";   // heap floor starving the fetch
    return "feed down";
  }
  return "no ac";                                                  // feed ok, nothing in range
}

void PageAircraft::onEnter(App& app) {
  _dirty = _needClear = true;
  _userSel = false; _sel = -1; _cycleMs = 0;   // restart auto-select/cycle each visit
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
    if (app.display().width() >= 640) pushTrails();   // breadcrumb trails: large screens only
  }
  _dirty = true;
}

// Append each live contact's current fix to its breadcrumb trail (matched by hex), and drop trails
// whose contact has left the feed. Bounded to 40 trails x K fixes so it never grows the heap.
void PageAircraft::pushTrails() {
  const auto& list = _ap.aircraft();
  for (const auto& a : list) {
    if (a.onGround) continue;
    Trail* t = nullptr;
    for (auto& tr : _trails) if (tr.hex == a.hex) { t = &tr; break; }
    if (!t) {
      if (_trails.size() >= 40) continue;
      _trails.push_back(Trail{}); t = &_trails.back(); t->hex = a.hex;
    }
    t->lat[t->head] = a.lat; t->lon[t->head] = a.lon;
    t->head = (t->head + 1) % Trail::K;
    if (t->n < Trail::K) t->n++;
  }
  for (size_t i = 0; i < _trails.size(); ) {            // prune contacts that have left
    bool live = false;
    for (const auto& a : list) if (a.hex == _trails[i].hex) { live = true; break; }
    if (live) ++i; else { _trails[i] = _trails.back(); _trails.pop_back(); }
  }
}

void PageAircraft::onTouch(App& app, int x, int y) {
  if (_view != 0) return;                          // Activity/Stats views: taps inert (swipe to navigate)
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
    int best = -1, bestd2 = (15 * app.ui()) * (15 * app.ui());
    for (int k = 0; k < n; ++k) {
      const Aircraft& a = list[_filt[k]];
      float aDist, aBrg; drPos(a, _lastDataMs, aDist, aBrg);
      float rr = aDist / _rMaxR * _rR; if (rr > _rR) rr = _rR;
      int ax = _rCx + (int)round(rr * sin(aBrg * D2R));
      int ay = _rCy - (int)round(rr * cos(aBrg * D2R));
      int d2 = (ax - x) * (ax - x) + (ay - ty) * (ay - ty);
      if (d2 < bestd2) { bestd2 = d2; best = k; }
    }
    if (best >= 0) { _sel = best; _userSel = true; _needClear = _dirty = true; return; }
  }
  int third = app.contentW() / 3;
  if (x < third)          { _sel = (_sel <= 0 ? n - 1 : _sel - 1); _userSel = true; _needClear = true; }
  else if (x > 2 * third) { _sel = (_sel + 1) % n;                 _userSel = true; _needClear = true; }
  _dirty = true;
}

bool PageAircraft::handleAltTap(App& app, int x, int yRel) {
  const int u = app.ui();
  if (x < 134 * u || x >= 215 * u || yRel < app.contentH() - 20 * u) return false;
  _altF = (_altF + 1) % 4; _sel = 0; _dirty = _needClear = true; return true;
}

bool PageAircraft::handleCatTap(App& app, int x, int yRel) {
  const int u = app.ui();
  if (x < 215 * u || yRel < app.contentH() - 20 * u) return false;
  _catF = (_catF + 1) % 5; _sel = 0; _dirty = _needClear = true; return true;
}

void PageAircraft::drawFilterBadges(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW();
  const int u = app.ui();
  int y = app.contentY() + app.contentH() - 16 * u;
  g.setTextSize(u);
  g.setTextDatum(textdatum_t::middle_left);
  g.fillRect(134 * u, y, 79 * u, 14 * u, gTheme.grid);
  g.setTextColor(_altF ? gTheme.ok : gTheme.fg, gTheme.grid);
  g.drawString(kAltLabel[_altF], 138 * u, y + 7 * u);
  g.fillRect(215 * u, y, cw - 215 * u - 2 * u, 14 * u, gTheme.grid);
  g.setTextColor(_catF ? gTheme.ok : gTheme.fg, gTheme.grid);
  g.drawString(kCatLabel[_catF], 219 * u, y + 7 * u);
}

bool PageAircraft::handleRadiusTap(App& app, int x, int yRel) {
  const int u = app.ui();
  if (x > 64 * u || yRel < app.contentH() - 20 * u) return false;
  int r = (int)_settings.getInt("adsbRadiusNm", 50);
  int next = (r <= 5) ? 10 : (r <= 10) ? 15 : (r <= 15) ? 25 : (r <= 25) ? 50 : 5;   // 5/10/15/25/50
  _settings.set("adsbRadiusNm", (long)next);
  _settings.save();
  _ap.poll();                                      // refetch with the new radius
  _dirty = _needClear = true;                      // ring label changed -> relayout
  return true;
}

bool PageAircraft::handleGroundTap(App& app, int x, int yRel) {
  const int u = app.ui();
  if (x < 68 * u || x > 132 * u || yRel < app.contentH() - 20 * u) return false;
  bool hide = _settings.getInt("adsbHideGround", 0) != 0;
  _settings.set("adsbHideGround", (long)(hide ? 0 : 1));
  _settings.save();
  _ap.poll();                                      // refetch / re-filter
  _dirty = _needClear = true;
  return true;
}

void PageAircraft::drawRadiusBadge(App& app) {
  auto& g = app.display().gfx();
  const int u = app.ui();
  int y = app.contentY() + app.contentH() - 16 * u;
  g.fillRect(4 * u, y, 56 * u, 14 * u, gTheme.grid);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(gTheme.fg, gTheme.grid);
  g.setTextSize(u);
  g.drawString(String((int)_ap.radiusNm()) + " nm", 8 * u, y + 7 * u);
}

void PageAircraft::drawGroundBadge(App& app) {
  auto& g = app.display().gfx();
  const int u = app.ui();
  int y = app.contentY() + app.contentH() - 16 * u;
  bool hide = _ap.hideGround();
  g.fillRect(64 * u, y, 64 * u, 14 * u, gTheme.grid);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(hide ? gTheme.dim : gTheme.fg, gTheme.grid);   // dim when filtered out
  g.setTextSize(u);
  g.drawString(hide ? "gnd: off" : "gnd: on", 68 * u, y + 7 * u);
}

void PageAircraft::tick(App& app, uint32_t nowMs) {
  // Auto-select the first contact and slowly cycle while the user is just
  // watching (MANUAL, nothing hand-picked). In AUTO the Director's autoAdvance
  // tours contacts instead, so don't double-step there.
  if (_view == 0 && !_userSel && app.mode() == App::Mode::Manual) {
    rebuildFilt();
    int n = (int)_filt.size();
    if (n > 0) {
      if (_sel < 0)                            { _sel = 0;            _cycleMs = nowMs; _needClear = _dirty = true; }
      else if (nowMs - _cycleMs >= kAutoCycleMs) { _sel = (_sel + 1) % n; _cycleMs = nowMs; _needClear = _dirty = true; }
    }
  }
  if (_dirty || nowMs - _lastDraw >= 1000) {        // full redraw on change / once a second
    _dirty = false; _lastDraw = nowMs; _marqMs = nowMs;
    draw(app);
    return;
  }
  if (_view == 0 && nowMs - _marqMs >= 50) { _marqMs = nowMs; drawAirportMarquee(app); }  // ~20fps ticker scroll (radar only)
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

// View 1: a classified contact list (the "arrivals board") — flight, type, altitude (hundreds of
// ft), climb/descend arrow, range, and what it's doing (DEP/ARR/local <field>, transit, on ground).
void PageAircraft::drawActivity(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  const auto& list = _ap.aircraft();
  rebuildFilt();
  if (_filt.empty()) {
    drawMessage(app, list.empty() ? (_ap.status() == ProviderStatus::Error ? "feed unavailable" : "no aircraft in range")
                                  : "no aircraft match filters", cy0);
    return;
  }
  const int u = app.ui();
  int x0 = 4 * u, y = cy0 + 2 * u;
  g.setTextDatum(textdatum_t::top_left); g.setTextSize(u);
  char hdr[80];
  snprintf(hdr, sizeof(hdr), "%-8.8s %-4.4s %3s%c %4s %s (%d)", "flight", "type", "alt", 'v', "rng", "activity", (int)_filt.size());
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString(padRight(String(hdr), 56), x0, y); y += 13 * u;

  const int rowH = 12 * u, bottom = cy0 + ch - 2 * u;
  for (int idx : _filt) {
    if (y + rowH > bottom) break;                              // fill the screen; Stats view totals the rest
    const Aircraft& a = list[idx];
    String apt; Activity act = classify(a, apt);
    char alt[6];
    if (a.onGround) strcpy(alt, "grd");
    else snprintf(alt, sizeof(alt), "%3d", (int)(drAlt(a, _lastDataMs) / 100.0f + 0.5f));   // dead-reckoned hundreds of ft
    char vs = a.vsFpm > 300 ? '^' : (a.vsFpm < -300 ? 'v' : '-');
    String id = a.flight.length() ? a.flight : a.hex;
    char buf[80];
    snprintf(buf, sizeof(buf), "%-8.8s %-4.4s %s%c %3dnm %s",
             id.c_str(), a.type.c_str(), alt, vs, (int)(a.distNm + 0.5f), actLabel(act, apt).c_str());
    g.setTextColor(actColor(act), gTheme.bg);
    g.drawString(padRight(String(buf), 56), x0, y);
    y += rowH;
  }
  if (y < cy0 + ch) g.fillRect(0, y, cw, cy0 + ch - y, gTheme.bg);   // clear trailing (already bg -> no flash)
}

// View 2: aggregate stats over the (filtered) contacts — activity + category breakdown and the
// closest / highest / fastest contact.
void PageAircraft::drawStats(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  const auto& list = _ap.aircraft();
  rebuildFilt();
  int dep = 0, arr = 0, trn = 0, gnd = 0, lcl = 0, air = 0, ga = 0, heli = 0, mil = 0, emerg = 0;
  float maxAlt = 0; int hi = -1; float minD = 1e9f; int nr = -1; float maxGs = 0; int fast = -1;
  for (int idx : _filt) {
    const Aircraft& a = list[idx];
    String apt;
    switch (classify(a, apt)) {
      case ActDeparting: dep++; break;  case ActArriving: arr++; break;
      case ActLocal:     lcl++; break;  case ActGround:   gnd++; break;
      case ActTransit:   trn++; break;  default: break;
    }
    switch (catClass(a.category)) { case 1: air++; break; case 2: ga++; break; case 3: heli++; break; case 4: mil++; break; }
    if (squawkAlert(a.squawk)) emerg++;
    if (!a.onGround) { float al = drAlt(a, _lastDataMs); if (al > maxAlt) { maxAlt = al; hi = idx; } }
    if (a.distNm < minD) { minD = a.distNm; nr = idx; }
    if (a.gsKt > maxGs) { maxGs = a.gsKt; fast = idx; }
  }
  const int u = app.ui();
  int x0 = 6 * u, y = cy0 + 4 * u;
  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(2 * u); g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString(padRight(String(_filt.size()) + " contacts", 24), x0, y); y += 22 * u;
  g.setTextSize(u);
  auto line = [&](const String& s, Color c) { g.setTextColor(c, gTheme.bg); g.drawString(padRight(s, 56), x0, y); y += 13 * u; };
  line(String("departing ") + dep + "   arriving " + arr + "   transit " + trn, gTheme.fg);
  line(String("local ") + lcl + "   on ground " + gnd, gTheme.dim);
  line(String("airliner ") + air + "  GA " + ga + "  heli " + heli + "  mil " + mil, gTheme.dim);
  if (emerg) line(String("EMERGENCY squawks: ") + emerg, gTheme.warn);
  y += 4 * u;
  auto pick = [&](const char* lbl, int idx, const String& extra) {
    if (idx < 0) { line(String(lbl) + " -", gTheme.dim); return; }
    String id = list[idx].flight.length() ? list[idx].flight : list[idx].hex;
    line(String(lbl) + " " + id + "  " + extra, gTheme.fg);
  };
  pick("closest", nr,   nr   >= 0 ? String((int)(list[nr].distNm + 0.5f)) + " nm" : "");
  pick("highest", hi,   hi   >= 0 ? String((int)(maxAlt / 100.0f + 0.5f) * 100) + " ft" : "");
  pick("fastest", fast, fast >= 0 ? String((int)(list[fast].gsKt + 0.5f)) + " kt" : "");
  if (y < cy0 + ch) g.fillRect(0, y, cw, cy0 + ch - y, gTheme.bg);
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
  // Build HOME + nearby-airport labels, then draw via the shared chip-row renderer.
  String labels[kMaxChips]; int n = 0, sel = 0;
  labels[n++] = "HOME";                      // HOME selected when not recentred on an airport
  for (const auto& s : st) {
    if (n >= kMaxChips) break;
    if (_centerIcao == s.icao) sel = n;
    labels[n++] = s.icao;
  }
  const int u = app.ui();
  _chipCount = app.drawChipRow(2 * u, app.contentY() + 3 * u, 13 * u, labels, n, sel, _chipX, _chipW, kMaxChips);
  for (int j = 0; j < _chipCount; ++j) _chipIcao[j] = (j == 0) ? String("") : labels[j];   // chip 0 = HOME
  return (13 + 5) * u;                        // 3 top gap + chip + 1 bottom
}

bool PageAircraft::handleChipTap(App& app, int x, int yRel) {
  if (yRel >= 14 * app.ui()) return false;         // chip row is the top band
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
  const int u = app.ui();
  if (!_loc.active().valid) { drawMessage(app, "no location", cy0); return; }
  if (_needClear) { g.fillRect(0, cy0, cw, ch, gTheme.bg); _needClear = false; }

  if (_view == 1) { drawActivity(app); return; }   // vertical-swipe sub-views
  if (_view == 2) { drawStats(app); return; }

  int chipH = drawChips(app);                      // centre selector (top)
  int top = cy0 + chipH;

  const auto& list = _ap.aircraft();
  if (list.empty()) {
    drawMessage(app, _ap.status() == ProviderStatus::Error ? "feed unavailable"
                  : _ap.status() == ProviderStatus::Loading ? "scanning..."
                  : _ap.hideGround() ? "no airborne aircraft in range"
                  : "no aircraft in range", top);
    drawAirportMarquee(app); // still useful with no traffic: what's tunable near you
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
    g.fillRect(0, ay0, cw, 14 * u, gTheme.warn);
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextColor(gTheme.bg, gTheme.warn);
    g.setTextSize(u);
    g.drawString(String("! ") + e.squawk + " " + squawkAlert(e.squawk) + ": " +
                 (e.flight.length() ? e.flight : e.hex) + "  " + (int)round(e.distNm) + "nm",
                 4 * u, ay0 + 7 * u);
    alertH = 15 * u;
  }
  top += alertH;

  // Radar on the left. Clear just the circle's bbox each tick (blips move);
  // the info column on the right redraws in place (padded) so it stays stable.
  const int MARQ = 16 * u;                           // reserve a ticker band at the bottom
  int size = min(ch - 8 * u - chipH - alertH - MARQ, cw / 2 - 8 * u);
  int R = size / 2 - 12 * u;                          // radar radius fills the left half (native)
  int cx = 8 * u + R + 8 * u, cy = top + (ch - chipH - alertH - MARQ) / 2;
  float maxR = _ap.radiusNm();
  _rCx = cx; _rCy = cy; _rR = R; _rMaxR = maxR;     // remember for tap-on-blip
  g.fillRect(cx - R - 4 * u, cy - R - 10 * u, 2 * R + 8 * u, 2 * R + 20 * u, gTheme.bg);

  g.drawCircle(cx, cy, R, gTheme.grid);              // outer ring = selected range
  // Fixed-distance reference rings (5 / 10 nm) when they fit inside the range, so
  // close-in traffic has a scale even at the 25/50 nm settings.
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextSize(u);
  for (int nm : {5, 10}) {
    if (nm >= (int)maxR) continue;
    int rr = (int)(nm / maxR * R);
    g.drawCircle(cx, cy, rr, gTheme.dim);
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(String(nm), cx, cy - rr - 4 * u);
  }
  g.drawFastHLine(cx - R, cy, 2 * R, gTheme.grid);
  g.drawFastVLine(cx, cy - R, 2 * R, gTheme.grid);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString("N", cx, cy - R - 6 * u);
  g.drawString(String((int)maxR) + "nm", cx + R - 8 * u, cy - 6 * u);

  // Shared radar centre (recentred airport, else the observer) for trails / airports / home marker.
  double clat = _loc.active().lat, clon = _loc.active().lon;
  if (_centerIcao.length())
    for (const auto& s : _wx.stations()) if (s.icao == _centerIcao) { clat = s.lat; clon = s.lon; break; }
  float coslat = cosf((float)clat * D2R);
  auto place = [&](double lat, double lon, int& px, int& py) -> bool {   // lat/lon -> radar pixel
    double dN = (lat - clat) * 60.0, dE = (lon - clon) * 60.0 * coslat;
    double d = sqrt(dN * dN + dE * dE);
    if (d > maxR) return false;
    double brg = atan2(dE, dN) / D2R, rr = d / maxR * R;
    px = cx + (int)round(rr * sin(brg * D2R));
    py = cy - (int)round(rr * cos(brg * D2R));
    return true;
  };

  // Breadcrumb trails (faded recent positions) drawn first, under the airports + blips.
  // Large-screen devices only (e.g. CrowPanel) — too dense on the small CYD radar.
  if (app.display().width() >= 640)
    for (const auto& tr : _trails) {
      int px, py;
      for (int j = 0; j < tr.n; ++j) {
        int idx = (tr.head - tr.n + j + 2 * Trail::K) % Trail::K;
        if (place(tr.lat[idx], tr.lon[idx], px, py)) g.fillCircle(px, py, 1 * u, gTheme.dim);
      }
    }

  // Nearby airports at their true radar position, marked + labelled in the ring colour.
  g.setTextDatum(textdatum_t::top_center);
  g.setTextSize(u);
  g.setTextColor(gTheme.grid, gTheme.bg);
  for (const auto& s : _wx.stations()) {
    int px, py;
    if (s.icao == _centerIcao || !place(s.lat, s.lon, px, py)) continue;   // centre field is the origin
    g.drawCircle(px, py, 2 * u, gTheme.grid);
    g.drawString(s.icao, px, py + 3 * u);
  }

  // Centre designator: what the radar is centred on (airport ICAO, or HOME), marked in the middle
  // like the other fields.
  {
    bool home = !_centerIcao.length();
    Color cc = home ? gTheme.accent : gTheme.grid;
    g.fillCircle(cx, cy, 2 * u, cc);
    g.setTextDatum(textdatum_t::top_center);
    g.setTextColor(cc, gTheme.bg);
    g.drawString(home ? String("HOME") : _centerIcao, cx, cy + 3 * u);
  }

  // HOME (us) marked when the radar is centred on an airport, so you keep your bearing back home.
  if (_centerIcao.length()) {
    int px, py;
    if (place(_loc.active().lat, _loc.active().lon, px, py)) {
      g.fillCircle(px, py, 2 * u, gTheme.accent);
      g.drawCircle(px, py, 5 * u, gTheme.accent);
      g.setTextDatum(textdatum_t::top_center);
      g.setTextColor(gTheme.accent, gTheme.bg);
      g.drawString("HOME", px, py + 6 * u);
    }
  }

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
    int tx = ax + (int)round(7 * u * sin(a.trackDeg * D2R));
    int ty = ay - (int)round(7 * u * cos(a.trackDeg * D2R));
    g.drawLine(ax, ay, tx, ty, c);
    g.fillCircle(ax, ay, (sel ? 3 : 2) * u, c);
    if (emerg) g.drawCircle(ax, ay, 6 * u, gTheme.warn);  // ring an emergency contact
    // ForeFlight-style data tag: altitude in hundreds of ft + a vertical-trend arrow
    // (filled triangle up=climb / down=descent, dash=level). Lets you read traffic at a glance.
    if (!a.onGround && a.altFt > 0) {
      Color tc = sel ? gTheme.ok : gTheme.dim;
      String fl = String((int)(drAlt(a, _lastDataMs) / 100.0f + 0.5f));    // dead-reckoned, hundreds of ft
      g.setTextDatum(textdatum_t::middle_left);
      g.setTextSize(u);
      g.setTextColor(tc, gTheme.bg);
      g.drawString(fl, ax + 5 * u, ay + 7 * u);
      int hx = ax + 7 * u + g.textWidth(fl), hy = ay + 7 * u;        // trend arrow after the number
      if (a.vsFpm > 256)       g.fillTriangle(hx, hy - 3 * u, hx - 3 * u, hy + 2 * u, hx + 3 * u, hy + 2 * u, tc);
      else if (a.vsFpm < -256) g.fillTriangle(hx, hy + 3 * u, hx - 3 * u, hy - 2 * u, hx + 3 * u, hy - 2 * u, tc);
      else                     g.drawFastHLine(hx - 3 * u, hy, 6 * u, tc);
    }
    {                                                 // flight number on every target: grey, selected green
      String cs = a.flight.length() ? a.flight : a.hex;
      g.setTextDatum(textdatum_t::bottom_left);
      g.setTextSize(u);
      g.setTextColor(sel ? gTheme.ok : gTheme.dim, gTheme.bg);
      g.drawString(cs, ax + 4 * u, ay - 2 * u);
    }
  }

  // Info column.
  int ix = cw / 2 + 8 * u, iy = top + 6 * u;
  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(u);
  auto line = [&](const String& s, Color col) { g.setTextColor(col, gTheme.bg); g.drawString(padRight(s, 20), ix, iy); iy += (u > 1 ? 13 : 14) * u; };  // CrowPanel: tighter so the block clears the marquee; CYD unchanged
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.setTextSize(2 * u); g.drawString("Aircraft", ix, iy); iy += 20 * u;
  g.setTextSize(u);
  line(String(_filt.size()) + "/" + list.size() + " @" + (_centerIcao.length() ? _centerIcao : String("HOME"))
       + "  " + (_ap.local() ? "local" : "cloud"), gTheme.dim);
  bool shownStale = false;                                              // own line (was overflowing)
  if (_ap.status() == ProviderStatus::Stale && _ap.lastFetched()) {
    int age = (int)(time(nullptr) - _ap.lastFetched());
    if (age > 0) { line("stale " + String(age) + "s", gTheme.warn); shownStale = true; }
  }
  if (!shownStale) iy += 14 * u;    // reserve the slot so the panel below doesn't reflow

  if (_sel >= 0 && _sel < (int)_filt.size()) {
    const Aircraft& a = list[_filt[_sel]];
    iy += (u > 1 ? 2 : 4) * u;         // CrowPanel: lift the contact block off the frequency marquee; CYD unchanged
    g.setTextColor(gTheme.ok, gTheme.bg);
    g.setTextSize(2 * u);
    g.drawString(a.flight.length() ? a.flight : a.hex, ix, iy); iy += (u > 1 ? 18 : 20) * u;
    g.setTextSize(u);
    line(String(_sel + 1) + "/" + _filt.size() + "  (tap edges)", gTheme.dim);
    if (a.type.length() || a.category.length())
      line(String("type ") + (a.type.length() ? a.type : a.category), gTheme.fg);
    line(a.onGround ? String("on ground") : String("alt ") + (int)drAlt(a, _lastDataMs) + " ft", gTheme.fg);
    if (!a.onGround) {
      bool lvl = fabsf(a.vsFpm) <= 256;
      line(lvl ? String("level flight")
               : String(a.vsFpm > 0 ? "climbing " : "descending ") + (int)fabsf(a.vsFpm) + " fpm",
           lvl ? gTheme.dim : (a.vsFpm > 0 ? gTheme.ok : gTheme.accent));
    }
    line(String("gs ") + (int)a.gsKt + " kt  trk " + (int)a.trackDeg, gTheme.fg);
    line(String("dist ") + (int)round(a.distNm) + " nm  brg " + (int)round(a.bearingDeg), gTheme.fg);
    // Look angle from the observer: az = bearing, el from altitude over ground range.
    if (!a.onGround) {
      double distFt = a.distNm * 6076.115;
      double el = distFt > 1 ? atan2((double)drAlt(a, _lastDataMs), distFt) / D2R : 90.0;
      line(String("look az ") + (int)round(a.bearingDeg) + "\xF7 el " + (int)round(el) + "\xF7", gTheme.accent);
    }
    if (a.squawk.length()) {
      const char* em = squawkAlert(a.squawk);
      line(String("squawk ") + a.squawk + (em ? String("  ") + em : String()), em ? gTheme.warn : gTheme.dim);
    }
  }

  drawAirportMarquee(app);
  drawRadiusBadge(app);
  drawGroundBadge(app);
  drawFilterBadges(app);
}

// Nearest airport + its likely frequencies (the ham/SDR headline). `full` lists every
// frequency (when the info column is free); otherwise a one-line summary. US dataset.
// Scrolling ticker (just above the filter chips): the nearest field + its full
// frequency list, marquee-scrolled so the whole list fits a single line. Redrawn at
// ~20fps from tick() between full page draws; offset is time-based so it's smooth.
void PageAircraft::drawAirportMarquee(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), ch = app.contentH(), cy0 = app.contentY();
  const int u = app.ui();
  int my = cy0 + ch - 29 * u;
  g.fillRect(0, my, cw, 12 * u, gTheme.bg);          // clear the ticker band
  if (!_loc.active().valid || !_adb.ready()) return;
  double oLat = _loc.active().lat, oLon = _loc.active().lon;
  // HOME -> closest field to the observer; a centred airport -> that field's freqs.
  const AirportDB::Nearest& a = _centerIcao.length() ? _adb.byId(_centerIcao.c_str(), oLat, oLon)
                                                     : _adb.nearest(oLat, oLon);
  if (!a.valid) {
    if (_centerIcao.length()) {                      // selected field not in the dataset
      g.setTextColor(gTheme.dim); g.setTextDatum(textdatum_t::top_left);
      g.drawString(_centerIcao + "  no freq data", 4 * u, my + 1 * u);
    }
    return;
  }
  if (!_centerIcao.length() && a.distNm >= 250) return;   // HOME but outside US coverage
  static const char* kDir[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  String m = String(a.id) + "  " + (int)round(a.distNm) + "nm " + kDir[((int)round(a.brgDeg / 45.0)) & 7] + "     ";
  char seg[24];
  for (int i = 0; i < a.n; ++i) {
    snprintf(seg, sizeof(seg), "%s %.2f    ", AirportDB::label(a.type[i]), a.f40[i] / 40.0f);
    m += seg;
  }
  g.setTextSize(u); g.setTextDatum(textdatum_t::top_left);
  int textW = g.textWidth(m);
  if (textW < 1) return;
  int off = (int)((millis() / 27) % (uint32_t)textW);   // ~37 px/s leftward scroll (75% speed)
  g.setTextColor(gTheme.accent);                        // transparent bg: copies coexist
  for (int x = 2 * u - off; x < cw; x += textW) g.drawString(m, x, my + 1 * u);
}
