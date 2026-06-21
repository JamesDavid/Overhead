#include "PageAviation.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../providers/AviationWxProvider.h"
#include "../providers/SoundingProvider.h"
#include "../providers/HazardProvider.h"
#include "../providers/WeatherProvider.h"
#include "../providers/PressureMapProvider.h"
#include "../assets/Coastline.h"
#include "../services/LocationService.h"
#include "../services/Settings.h"
#include "../services/MetarStore.h"
#include <math.h>
#include <functional>

static constexpr double D2R = 3.14159265358979323846 / 180.0;
static void windBarb(lgfx::LovyanGFX& g, int x, int y, int wdir, int wspd, Color col);  // fwd (defined below)

String PageAviation::gridStatus() {
  const auto& st = _wx.stations();               // sorted by distance: [0] is nearest
  if (st.empty()) return String();
  const auto& m = st[0];                          // as much decoded METAR as fits (grid word-wraps)
  String s = m.icao;
  if (m.cat.length())   s += " " + m.cat;
  if (m.tempC > -999)   s += " " + String(m.tempC) + "C(" + String(m.tempC * 9 / 5 + 32) + "F)";
  if (m.wspd == 0)      s += " calm";
  else if (m.wspd > 0)  s += " " + (m.wdir >= 0 ? String(m.wdir) : String("VRB")) + "@" + String(m.wspd) + "kt";
  if (m.cloud >= 0)     s += " cld" + String(m.cloud) + "%";
  if (m.ceilingFt >= 0) s += " cig" + String(m.ceilingFt / 100);
  if (m.wx.length())    s += " " + m.wx;
  return s;
}

static Color catColor(const String& c) {
  if (c == "VFR")  return gTheme.ok;
  if (c == "MVFR") return gTheme.accent;
  return gTheme.warn;   // IFR / LIFR
}


static int drawWrapped(LGFX& g, const String& text, int x, int y, int maxChars, int maxLines, Color c) {
  g.setTextColor(c, gTheme.bg);
  g.setTextDatum(textdatum_t::top_left);
  int line = 0, i = 0, n = text.length();
  while (i < n && line < maxLines) {
    int end = i + maxChars;
    if (end >= n) end = n; else { int sp = text.lastIndexOf(' ', end); if (sp > i) end = sp; }
    g.drawString(text.substring(i, end), x, y);
    y += 11; line++;
    i = end + (end < n && text[end] == ' ' ? 1 : 0);
  }
  return y;
}

void PageAviation::onEnter(App& app) {
  _presMode = (int)_settings.getInt("presMode", 0);   // layers: 0 cat / 1 cloud / 2 wind / 3 inHg
  if (_presMode < 0 || _presMode > 3) _presMode = 0;
  _pmap.setScope((int)_settings.getInt("presScope", 0));   // default regional (~200mi)
  resetPresZoom();
  _dirty = _needClear = true;
}

// Pressure-map zoom levels (index by _pZoomLevel; 0 = full extent). Tapping the map
// steps through these about the tapped point, wrapping back to off.
static const float kPZoomF[] = { 1.0f, 2.6f, 4.5f, 7.0f };

void PageAviation::cyclePresZoom(int fx, int fy) {
  _pZoomLevel = (_pZoomLevel + 1) % kPZoomN;
  _pZoomFrom = _pZoomCur;
  _pFx = fx; _pFy = fy;
  _pZoom = _pZoomLevel > 0;
  _pZoomMs = millis();
  _needClear = _dirty = true;
}

void PageAviation::focusSpeci() {
  const auto& st = _wx.stations();
  for (int i = 0; i < (int)st.size(); ++i)
    if (st[i].raw.startsWith("SPECI")) { _sel = i; _view = View::Metar; _needClear = _dirty = true; return; }
}

// Director notice landing: hazards view if there's an advisory, else the METAR card
// (which carries the SPECI / extreme present-weather line).
void PageAviation::focusWxNotice() {
  _view = anyHazard() ? View::Hazards : View::Metar;
  _needClear = _dirty = true;
}

// Ordered list of currently-visible views. TAF is hidden when no field has one;
// Hazards is hidden when there's no AIRMET/SIGMET/PIREP nearby (a hazard surfaces
// instead as a Director notice). So the carousel + view dots only show real views.
int PageAviation::activeViews(View* out) const {
  static const View all[] = { View::Pressure, View::Metar, View::Taf, View::Sounding, View::Hazards, View::Trends };
  int n = 0;
  for (View v : all) {
    if (v == View::Taf && !anyTaf()) continue;
    if (v == View::Hazards && !anyHazard()) continue;
    out[n++] = v;
  }
  return n;
}

void PageAviation::stepView(int dir) {
  View vs[7]; int n = activeViews(vs);
  int cur = 0; for (int i = 0; i < n; ++i) if (vs[i] == _view) { cur = i; break; }
  cur = (cur + dir + n) % n; _view = vs[cur];
  if (_view == View::Taf) enterTaf();          // point _sel at a TAF-bearing field
  resetPresZoom();
  _mapSelIcao = "";                            // drop any map station pick on a view change
  _needClear = _dirty = true;
}

void PageAviation::cycleView(int dir) { stepView(dir); }   // up/down swipe -> next/prev view

// Full decoded METAR for an ICAO from the regional fetch, or null (e.g. a US/world
// dot the bbox query never pulled raw text for).
const Metar* PageAviation::findStation(const String& icao) const {
  for (const auto& s : _wx.stations()) if (s.icao == icao) return &s;
  return nullptr;
}

// The unified map always reserves a bottom strip for a station's METAR when any field
// is loaded (selected or nearest); only a truly empty map falls back to a bare legend.
int PageAviation::mapBottomH() const {
  return (_mapSelIcao.length() || !_wx.stations().empty()) ? 40 : 12;
}

// US/world pressure map: convert a tapped screen point to lat/lon and fetch the
// local airports around it (drill into the region with regional density).
void PageAviation::drillPressure(App& app, int absX, int absY) {
  double w0, w1, a0, a1; _pmap.bbox(w0, w1, a0, a1);
  const int mx = 2, my = app.contentY() + 16, mw = app.contentW() - 4, mh = app.contentH() - 16 - mapBottomH();
  double zs = _pZoomCur;                                    // undo any active zoom
  double ux = (absX - _pFx) / zs + _pFx, uy = (absY - _pFy) / zs + _pFy;
  double lon = w0 + (ux - mx) / (double)mw * (w1 - w0);
  double lat = a1 - (uy - my) / (double)mh * (a1 - a0);
  _pmap.fetchAround(lat, lon);
  resetPresZoom();                                          // now a regional box; clear zoom
  _needClear = _dirty = true;
}

bool PageAviation::anyTaf() const {
  for (const auto& s : _wx.stations()) if (s.taf.length()) return true;
  return false;
}
bool PageAviation::anyHazard() const { return !_haz.hazards().empty(); }

// View-dot count/index follow the visible set, so TAF/Hazards drop out cleanly when
// empty (no never-landed-on gap dot).
int PageAviation::viewCount() const { View vs[7]; return activeViews(vs); }
int PageAviation::viewIndex() const {
  View vs[7]; int n = activeViews(vs);
  for (int i = 0; i < n; ++i) if (vs[i] == _view) return i;
  return 0;
}

bool PageAviation::enterTaf() {
  const auto& st = _wx.stations(); int n = (int)st.size();
  if (_sel >= 0 && _sel < n && st[_sel].taf.length()) return true;   // current field already has one
  for (int i = 0; i < n; ++i) if (st[i].taf.length()) { _sel = i; return true; }
  return false;
}
int PageAviation::nextTaf(int from, int dir) const {
  const auto& st = _wx.stations(); int n = (int)st.size();
  for (int k = 1; k <= n; ++k) { int i = ((from + dir * k) % n + n) % n; if (st[i].taf.length()) return i; }
  return -1;
}

void PageAviation::onData(App& app, ProviderId id) {
  int n = (int)_wx.stations().size();
  if (_sel >= n) _sel = n ? n - 1 : 0;
  bool empty = (n == 0);
  if (empty != _wasEmpty) { _wasEmpty = empty; }
  if ((_view == View::Taf && !anyTaf()) || (_view == View::Hazards && !anyHazard()))
    _view = View::Metar;                 // current view just became hidden -> fall back
  _dirty = _needClear = true;
}

void PageAviation::onTouch(App& app, int x, int y) {
  if (_view == View::Metar && y < 16) {              // tap a field chip -> select that station
    for (int i = 0; i < _mChipN; ++i)
      if (x >= _mChipX[i] && x < _mChipX[i] + _mChipW[i]) { _sel = _mChipScroll + i; _needClear = _dirty = true; return; }
  }
  int third = app.contentW() / 3;
  if (x >= third && x <= 2 * third && _view != View::Pressure) {  // centre tap cycles the view
    stepView(+1); return;                           // (Pressure: tap the MAP to drill/zoom at that point)
  }
  if (_view == View::Pressure && x < 60 && y < 16) { // top-left badge: hPa->inHg->cloud->category
    _presMode = (_presMode + 1) % 4;
    _settings.set("presMode", (long)_presMode); _settings.save();   // persist preference
    _needClear = _dirty = true; return;
  }
  if (_view == View::Pressure && x > app.contentW() - 82 && x <= app.contentW() - 50 && y < 16) {  // recenter home @1x
    if (_loc.active().valid) {
      _pmap.fetchAround(_loc.active().lat, _loc.active().lon);   // regional box centred on the observer
      resetPresZoom();                                           // back to 1x
      _mapSelIcao = "";
      _needClear = _dirty = true;
    }
    return;
  }
  if (_view == View::Pressure && x > app.contentW() - 50 && y < 16) {  // top-right: zoom 200mi/US/world
    int ns = (_pmap.scope() + 1) % 3;
    _pmap.setScope(ns);
    _settings.set("presScope", (long)ns); _settings.save();
    resetPresZoom();                                   // reset map zoom on scope change
    _mapSelIcao = "";                                  // selection no longer in this bbox
    _needClear = _dirty = true; return;
  }
  if (_view == View::Pressure && y >= 16) {            // tap the map
    int absX = x, absY = y + app.contentY();
    // US/world: a tap drills into that point, fetching the regional airports there to
    // populate data (the wide views are sparse on their own).
    if (_pmap.scope() != 0) { drillPressure(app, absX, absY); return; }
    // Regional (200mi): zoom about the tapped point until we're zoomed in far enough
    // that the dots are well separated -- only THEN does tapping a dot select it (its
    // METAR shows in the bottom strip; re-tap to clear). Below that, every tap zooms.
    int best = -1, bestD2 = 16 * 16;     // generous target: dots are far apart when zoomed in
    if (_pZoomCur > 2.0f) for (int i = 0; i < _mapDotN; ++i) {
      int dx = absX - _mapDotX[i], dy = absY - _mapDotY[i], d2 = dx * dx + dy * dy;
      if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    if (best >= 0) {
      _mapSelIcao = (_mapSelIcao == _mapDotIcao[best]) ? String("") : _mapDotIcao[best];
      _needClear = _dirty = true; return;
    }
    cyclePresZoom(absX, absY); return;      // regional: step through zoom levels
  }
  int n = (int)_wx.stations().size();
  if (n && (_view == View::Metar || _view == View::Taf)) {   // edges step stations
    if (_view == View::Taf) { int t = nextTaf(_sel, x < third ? -1 : 1); if (t >= 0) _sel = t; }  // TAF fields only
    else _sel = (x < third) ? (_sel - 1 + n) % n : (_sel + 1) % n;
    _needClear = _dirty = true;
  }
}

bool PageAviation::autoAdvance(App& app) {
  bool cycled = false;
  auto nextView = [&]() {
    View vs[7]; int n = activeViews(vs);
    int cur = 0; for (int i = 0; i < n; ++i) if (vs[i] == _view) { cur = i; break; }
    bool wasLast = (cur == n - 1);
    _view = vs[(cur + 1) % n];
    _tourN = 0; _sel = 0;
    if (_view == View::Taf) enterTaf();
    if (_view == View::Pressure) _presTourStep = -1;   // restart the pressure cinematic
    if (wasLast) cycled = true;            // wrapped past the last visible view = full cycle
  };
  int n = (int)_wx.stations().size();
  if (_view == View::Taf && n > 0) {       // tour only TAF-bearing fields
    int t = nextTaf(_sel, 1);
    if (t < 0 || t <= _sel) nextView(); else _sel = t;
  } else if (_view == View::Metar && n > 0) {
    _sel = (_sel + 1) % n;
    if (++_tourN >= n) nextView();         // toured all stations -> next view
  } else if (_view == View::Pressure) {
    // Cinematic: cycle all four layers at 1x (regional, centred on the observer), then
    // zoom into the observer step by step (2.6x -> 4.5x -> 7x). At regional scope the
    // observer sits at the box centre, so zooming about the screen centre = on the user.
    int fx = app.contentW() / 2, fy = app.contentY() + 16 + (app.contentH() - 28) / 2;
    switch (++_presTourStep) {
      case 0: _pmap.setScope(0); resetPresZoom(); _presMode = 0; break;   // 1x, category
      case 1: _presMode = 1; break;                                       // 1x, cloud
      case 2: _presMode = 2; break;                                       // 1x, wind
      case 3: _presMode = 3; break;                                       // 1x, inHg
      case 4: case 5: case 6: cyclePresZoom(fx, fy); break;              // zoom in on the observer
      default: _presTourStep = -1; nextView(); break;                     // done -> next view
    }
  } else {
    nextView();                            // Sounding/Hazards (no items): one dwell, next view
  }
  _needClear = _dirty = true;
  return cycled;
}

void PageAviation::tick(App& app, uint32_t nowMs) {
  float zTgt = kPZoomF[_pZoomLevel];             // ease the pressure map toward the level's factor
  if (fabs(_pZoomCur - zTgt) > 0.001f) {
    const uint32_t DUR = 260;
    float p = (nowMs - _pZoomMs) >= DUR ? 1.f : (float)(nowMs - _pZoomMs) / DUR;
    float e = p * p * (3 - 2 * p);
    _pZoomCur = _pZoomFrom + e * (zTgt - _pZoomFrom);
    if (p >= 1.f) _pZoomCur = zTgt;
    _needClear = _dirty = true;
  }
  if (_view == View::Pressure && _pmap.points().empty() && nowMs - _presRetryMs > 8000) {
    _presRetryMs = nowMs; _pmap.refresh(false);  // keep retrying an un-cached scope (e.g. US) until heap allows
  }
  if (!_dirty && nowMs - _lastDraw < 5000) return;
  _dirty = false; _lastDraw = nowMs;
  draw(app);
}

void PageAviation::draw(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  if (_needClear) { g.fillRect(0, cy0, cw, ch, gTheme.bg); _needClear = false; }
  if (_view == View::Metar)         drawMetar(app);
  else if (_view == View::Taf)      drawTaf(app);
  else if (_view == View::Sounding) drawSounding(app);
  else if (_view == View::Hazards)  drawHazards(app);
  else if (_view == View::Trends)   drawTrends(app);
  else                              drawPressure(app);
}

void PageAviation::drawMetar(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  const auto& list = _wx.stations();
  if (list.empty()) {
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(!_loc.active().valid ? "no location"
               : _wx.status() == ProviderStatus::Error ? "AWC unavailable" : "loading METARs...",
                 cw / 2, cy0 + ch / 2);
    return;
  }
  if (_sel >= (int)list.size()) _sel = 0;
  // Field-selector chips: window them so the selected field is always visible with a
  // neighbour either side (scrolls as you step near an edge) — you're never stuck at
  // the visible edge unless it's the real end of the list.
  String labels[12]; int nl = (int)list.size(); if (nl > 12) nl = 12;
  for (int i = 0; i < nl; ++i) labels[i] = list[i].icao;
  auto lastFit = [&](int start) {                                  // last chip index drawn from `start`
    int x = 2, last = start;                                       // (bounded by width AND the kMChips cap,
    for (int i = start; i < nl && i < start + kMChips; ++i) {       // matching App::drawChipRow exactly)
      int w = (int)labels[i].length() * 6 + 8; if (x + w > cw - 2) break; last = i; x += w + 3;
    }
    return last;
  };
  if (_sel < _mChipScroll) _mChipScroll = _sel;                    // selection left of window
  while (_sel > lastFit(_mChipScroll) && _mChipScroll < nl - 1) _mChipScroll++;  // selection right of window
  if (_sel == lastFit(_mChipScroll) && _sel < nl - 1) _mChipScroll++;            // reveal next neighbour
  else if (_sel == _mChipScroll && _mChipScroll > 0)  _mChipScroll--;            // reveal prev neighbour
  if (_mChipScroll < 0) _mChipScroll = 0;
  int first = _mChipScroll;
  _mChipN = app.drawChipRow(2, cy0 + 2, 13, labels + first, nl - first, _sel - first, _mChipX, _mChipW, kMChips);

  const Metar& m = list[_sel];
  const int maxc = (cw - 10) / 6;
  int x0 = 6, y = cy0 + 19;                                    // below the chip row

  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.setTextSize(2); g.drawString(m.icao, x0, y);
  g.setTextDatum(textdatum_t::top_right);
  g.setTextColor(catColor(m.cat), gTheme.bg); g.drawString(m.cat, cw - 6, y + 2);
  g.setTextDatum(textdatum_t::top_left); g.setTextSize(1);
  y += 20;
  auto line = [&](const String& s, Color c) { g.setTextColor(c, gTheme.bg); g.drawString(padRight(s, maxc), x0, y); y += 12; };

  // Observation time: Zulu + local, e.g. 160354Z / 8:54PM.
  if (m.obsTime) {
    struct tm gu, lo; time_t t = m.obsTime; gmtime_r(&t, &gu); localtime_r(&t, &lo);
    char z[10], l[10]; strftime(z, sizeof(z), "%d%H%MZ", &gu); strftime(l, sizeof(l), "%I:%M%p", &lo);
    line(String(z) + " / " + l, gTheme.dim);
  }
  line(m.name.substring(0, maxc) + "  " + (int)round(m.distNm) + "nm  [tap mid: taf]", gTheme.dim);
  if (m.wspd >= 0) line(m.wspd == 0 ? String("wind calm")
        : String("wind ") + (m.wdir < 0 ? String("VRB") : String(m.wdir)) + "\xF7 @ " + m.wspd
          + " kt (" + (int)round(m.wspd * 1.15078f) + " mph)", gTheme.fg);
  if (m.visSm >= 0)   line(String("vis ") + String(m.visSm, 0) + " sm", gTheme.fg);
  line(m.ceilingFt >= 0 ? String("ceiling ") + m.ceilingFt + " ft" : String("ceiling: none"), gTheme.fg);
  if (m.tempC > -999) line(String("temp ") + m.tempC + " (" + (m.tempC * 9 / 5 + 32) + "F)  dewpt "
                           + m.dewpC + " (" + (m.dewpC * 9 / 5 + 32) + "F)", gTheme.fg);
  if (m.altimHpa > 0) line(String("QNH ") + m.altimHpa + " hPa (" + String(m.altimHpa / 33.8639f, 2) + " inHg)", gTheme.fg);
  if (m.wx.length())  line(String("wx ") + m.wx, gTheme.warn);
  line(String(_sel + 1) + "/" + list.size(), gTheme.dim);
  y += 3; g.setTextColor(gTheme.dim, gTheme.bg); g.drawString("METAR", x0, y); y += 11;
  y = drawWrapped(g, m.raw, x0, y, maxc, 3, gTheme.fg);
  if (m.taf.length() && y < cy0 + ch - 24) {
    y += 2; g.setTextColor(gTheme.dim, gTheme.bg); g.drawString("TAF", x0, y); y += 11;
    drawWrapped(g, m.taf, x0, y, maxc, (cy0 + ch - y) / 11, gTheme.dim);
  }
}

// --- compact TAF token decode ---
static String tafWind(const String& t) {
  if (!t.endsWith("KT") || t.length() < 7) return "";
  if (t == "00000KT") return "calm";
  String dir = t.substring(0, 3);
  if (dir != "VRB") for (int k = 0; k < 3; ++k) if (!isdigit(dir[k])) return "";
  int gi = t.indexOf('G');
  String spd = t.substring(3, gi > 0 ? gi : t.length() - 2);
  String s = dir + "@" + String(spd.toInt());
  if (gi > 0) s += "g" + String(t.substring(gi + 1, t.length() - 2).toInt());
  return s + "kt";
}
static String tafVis(const String& t) {
  if (!t.endsWith("SM") || t.length() < 3) return "";
  String v = t.substring(0, t.length() - 2), pre;
  if (v.startsWith("P")) { pre = ">"; v = v.substring(1); }
  else if (v.startsWith("M")) { pre = "<"; v = v.substring(1); }
  if (!v.length() || !(isdigit(v[0]))) return "";
  return pre + v + "sm";
}
static String tafTok(const String& t) {                 // wind/vis decoded; rest passed through
  String w = tafWind(t); if (w.length()) return w;
  String v = tafVis(t);  if (v.length()) return v;
  return t;                                              // FEW/SCT/BKN/OVC, wx, CAVOK, SKC...
}

void PageAviation::drawTaf(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  const auto& list = _wx.stations();
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  if (list.empty() || _sel >= (int)list.size()) {
    g.drawString("TAF  [tap mid: sounding]", 6, cy0 + 2);
    g.setTextColor(gTheme.dim, gTheme.bg); g.drawString("no station", 6, cy0 + ch / 2); return;
  }
  const Metar& m = list[_sel];
  g.drawString(m.icao + " TAF  [tap mid: sounding]", 6, cy0 + 2);
  if (!m.taf.length()) { g.setTextColor(gTheme.dim, gTheme.bg); g.drawString("no TAF for this field", 6, cy0 + ch / 2); return; }

  const String& t = m.taf; int i = 0, n = t.length();
  int y = cy0 + 16; const int maxc = (cw - 10) / 6;
  String label = "now", parts, tok; bool gotIcao = false;
  auto nextTok = [&](String& o) -> bool { while (i < n && t[i] == ' ') i++; if (i >= n) return false; int s = i; while (i < n && t[i] != ' ') i++; o = t.substring(s, i); return true; };
  auto rng = [&](const String& r) -> String { return (r.length() == 9 && r[4] == '/') ? r.substring(2, 4) + "-" + r.substring(7, 9) : r; };
  auto emit = [&]() {
    if (!parts.length() || y > cy0 + ch - 12) { parts = ""; return; }
    g.setTextColor(gTheme.warn, gTheme.bg); g.drawString(padRight(label, 9).substring(0, 9), 6, y);
    g.setTextColor(gTheme.fg, gTheme.bg);   g.drawString(parts.substring(0, maxc - 9), 6 + 9 * 6, y);
    y += 12; parts = "";
  };
  bool haveValid = false;
  while (nextTok(tok)) {
    if (tok == "TAF" || tok == "AMD" || tok == "COR") continue;
    if (!gotIcao && tok.length() == 4) { gotIcao = true; continue; }     // ICAO
    if (tok.endsWith("Z") && tok.length() >= 6) continue;                // issue time
    if (!haveValid && tok.length() == 9 && tok[4] == '/') { haveValid = true; continue; }  // overall valid
    if (tok.startsWith("FM") && tok.length() == 8) { emit(); label = "FM" + tok.substring(2, 4) + "z"; continue; }
    if (tok == "BECMG") { emit(); String r; label = nextTok(r) ? "bcmg " + rng(r) : "becmg"; continue; }
    if (tok == "TEMPO") { emit(); String r; label = nextTok(r) ? "tmpo " + rng(r) : "tempo"; continue; }
    if (tok.startsWith("PROB")) { emit(); String r; label = "p" + tok.substring(4); if (nextTok(r)) label += " " + rng(r); continue; }
    parts += tafTok(tok) + " ";
  }
  emit();
  if (y == cy0 + 16) { g.setTextColor(gTheme.dim, gTheme.bg); g.drawString("(could not parse)", 6, y); }
}

void PageAviation::drawSounding(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  const auto& lv = _snd.levels();
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString(String("Model sounding @") + _loc.active().name + "  [tap: hazards]", 6, cy0 + 2);
  if (lv.size() < 3) {
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(_snd.status() == ProviderStatus::Error ? "sounding unavailable" : "loading sounding...", 6, cy0 + ch / 2);
    return;
  }

  // Plot box: x = temp -40..40 C, y = altitude 0..40 kft (aviation-native units).
  const float M2FT = 3.28084f;
  const int gx = 24, gy = cy0 + 16, gw = cw - 90, gh = ch - 40;   // bottom rows: legend + analysis
  const float Tmin = -40, Tmax = 40, AmaxFt = 40000;
  auto px  = [&](float T)  { return gx + (int)((T - Tmin) / (Tmax - Tmin) * gw); };
  auto pyf = [&](float ft) { return gy + gh - (int)(fminf(ft, AmaxFt) / AmaxFt * gh); };
  auto pym = [&](float m)  { return pyf(m * M2FT); };
  auto dash = [&](int x0, int y0, int x1, int y1, Color c) {     // dashed straight line
    int n = max(abs(x1 - x0), abs(y1 - y0)) / 4; if (n < 1) n = 1;
    for (int k = 0; k < n; k += 2) {
      float f0 = (float)k / n, f1 = (float)(k + 1) / n;
      g.drawLine(x0 + (int)((x1 - x0) * f0), y0 + (int)((y1 - y0) * f0),
                 x0 + (int)((x1 - x0) * f1), y0 + (int)((y1 - y0) * f1), c);
    }
  };
  g.drawRect(gx, gy, gw, gh, gTheme.grid);
  g.setTextColor(gTheme.dim, gTheme.bg);
  if (px(0) > gx && px(0) < gx + gw) g.drawFastVLine(px(0), gy, gh, gTheme.grid);  // 0 C
  for (int kft = 10; kft <= 30; kft += 10) { int yy = pyf(kft * 1000); g.drawFastHLine(gx, yy, gw, gTheme.grid); g.drawString(String(kft), 4, yy - 3); }

  // Dashed dry-adiabat parcel from the surface (where it crosses temp = top of lift).
  if (!lv.empty()) {
    float a0 = lv.front().altM, t0 = lv.front().tempC, topM = AmaxFt / M2FT;
    dash(px(t0), pym(a0), px(t0 - 9.8f * (topM - a0) / 1000.0f), pyf(AmaxFt), gTheme.dim);
  }

  int px0t = -1, py0t = 0, px0d = -1, py0d = 0;
  for (const auto& l : lv) {
    if (l.altM * M2FT > AmaxFt) break;
    int xt = px(l.tempC), yt = pym(l.altM);
    if (px0t >= 0) g.drawLine(px0t, py0t, xt, yt, gTheme.warn);
    px0t = xt; py0t = yt;
    if (l.dewpC > -900) { int xd = px(l.dewpC), yd = pym(l.altM); if (px0d >= 0) g.drawLine(px0d, py0d, xd, yd, gTheme.accent); px0d = xd; py0d = yd; }
  }

  // Freezing level.
  if (_snd.freezingLevelM() > 0) {
    int yf = pym(_snd.freezingLevelM());
    g.drawFastHLine(gx, yf, gw, gTheme.ok);
    g.setTextColor(gTheme.ok, gTheme.bg);
    g.setTextDatum(textdatum_t::bottom_right);
    g.drawString(String("FZL ") + (int)(_snd.freezingLevelM() * M2FT) + "ft", cw - 2, yf);
    g.setTextDatum(textdatum_t::top_left);
  }

  // Winds aloft (nearest levels to 3/10/20/30 kft), each at its own altitude's y.
  const int targets[] = {3000, 10000, 20000, 30000};
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.setTextDatum(textdatum_t::middle_left);
  for (int t : targets) {
    float tgtM = t / M2FT; const SoundingLevel* best = nullptr; float bd = 1e9;
    for (const auto& l : lv) if (l.wspd >= 0) { float d = fabsf(l.altM - tgtM); if (d < bd) { bd = d; best = &l; } }
    if (best) {
      char b[24]; snprintf(b, sizeof(b), "%dk %03d@%dkt", t / 1000, best->wdir, best->wspd);
      g.setTextDatum(textdatum_t::middle_right);
      g.drawString(b, cw - 2, pyf(best->altM * M2FT));    // altitude label + wind, at that altitude's y
    }
  }
  g.setTextDatum(textdatum_t::top_left);

  // Legend.
  int lgy = cy0 + ch - 22, lx = 4;
  auto key = [&](Color c, const String& s) {
    g.fillCircle(lx + 2, lgy + 3, 2, c); lx += 8;
    g.setTextDatum(textdatum_t::top_left); g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(s, lx, lgy); lx += (int)s.length() * 6 + 6;
  };
  key(gTheme.warn, "temp"); key(gTheme.accent, "dewpt"); key(gTheme.ok, "FZL");
  key(gTheme.dim, "parcel"); g.drawString("x:C y:kft", lx, lgy);

  // Analysis line (model estimates): stability / cloud base / lift top / inversion.
  static const char* kStab[] = {"stable", "neutral", "unstable", "strong lift"};
  auto kft = [](float ft) { return String(ft / 1000.0f, 1) + "k"; };
  String a;
  if (_snd.stability() >= 0) a = kStab[_snd.stability()];
  if (_snd.cloudBaseFt() > 0)  a += (a.length() ? " \xB7 " : "") + String("base ") + kft(_snd.cloudBaseFt());
  float surfFt = lv.front().altM * M2FT;
  if (_snd.thermalTopFt() > surfFt + 200) a += " \xB7 top " + kft(_snd.thermalTopFt());
  if (_snd.inversionFt() > 0)  a += " \xB7 inv " + kft(_snd.inversionFt());
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.drawString(a.length() ? a : String("(analysis n/a)"), 4, cy0 + ch - 10);
}

void PageAviation::drawHazards(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  const int maxc = (cw - 10) / 6;
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString("Hazards  [tap mid: map]", 6, cy0 + 2);
  const auto& hz = _haz.hazards();
  int y = cy0 + 16;
  if (hz.empty()) {
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString("no AIRMET/SIGMET/PIREP nearby", 6, cy0 + ch / 2);
    return;
  }
  for (const auto& h : hz) {
    if (y > cy0 + ch - 12) break;
    y = drawWrapped(g, (h.pirep ? "PIREP " : "") + h.text, 6, y, maxc, 3, h.pirep ? gTheme.dim : gTheme.warn);
    y += 3;
  }
}

// Area weather trends from the Open-Meteo hourly series (one request, shared with
// the Agenda Sky Window): temp / dewpoint / cloud / pressure sparklines over the
// next ~24h, plus a rule-based conclusion. "Area" = the observer location (Open-
// Meteo is point-based); per-airport current values live in the METAR card.
void PageAviation::drawTrends(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(1);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.drawString("Area trends 24h  [tap mid: map]", 4, cy0 + 2);

  const auto& temp = _wxo.tempSeries();
  const auto& dewp = _wxo.dewpSeries();
  const auto& cloud = _wxo.cloudSeries();
  const auto& pres = _wxo.presSeries();
  time_t base = _wxo.baseTime();
  if (base == 0 || temp.size() < 3 || pres.size() < 3) {
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(_wxo.status() == ProviderStatus::Error ? "weather unavailable" : "loading trends...", 4, cy0 + ch / 2);
    return;
  }
  const int SENT = -32768;
  auto gT = [&](int i) { return (i >= 0 && i < (int)temp.size())  ? (int)temp[i]  : SENT; };
  auto gD = [&](int i) { return (i >= 0 && i < (int)dewp.size())  ? (int)dewp[i]  : SENT; };
  auto gC = [&](int i) { return (i >= 0 && i < (int)cloud.size()) ? (int)cloud[i] : SENT; };
  auto gP = [&](int i) { return (i >= 0 && i < (int)pres.size())  ? (int)pres[i]  : SENT; };

  int now = (int)((time(nullptr) - base) / 3600); if (now < 0) now = 0;
  const int span = 24;
  const int gx = 62, gw = cw - gx - 6;
  const int top = cy0 + 15, rowH = (ch - 15 - 15) / 4;

  auto sparkRow = [&](int r, const char* name, std::function<int(int)> get, const String& cur, const String& cur2, Color c) {
    int y0 = top + r * rowH;
    int mn = 32767, mx = -32768;
    for (int i = now; i <= now + span; ++i) { int v = get(i); if (v == SENT) continue; if (v < mn) mn = v; if (v > mx) mx = v; }
    if (mn > mx) return;
    if (mn == mx) mx = mn + 1;
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(gTheme.dim, gTheme.bg); g.drawString(name, 4, y0);
    g.setTextColor(c, gTheme.bg);          g.drawString(cur, 4, y0 + 10);
    if (cur2.length()) { g.setTextColor(gTheme.dim, gTheme.bg); g.drawString(cur2, 4, y0 + 20); }  // imperial
    int ph = rowH - 6, py = y0 + 2, px = -1, pv = 0;
    g.drawFastVLine(gx, py, ph, gTheme.grid);          // "now" baseline
    for (int i = now; i <= now + span; ++i) {
      int v = get(i); if (v == SENT) { px = -1; continue; }
      int x = gx + (int)((long)(i - now) * gw / span);
      int yy = py + ph - (int)((long)(v - mn) * ph / (mx - mn));
      if (px >= 0) g.drawLine(px, pv, x, yy, c);
      px = x; pv = yy;
    }
  };

  char cv[16], cv2[16];
  int tc = gT(now) == SENT ? 0 : gT(now), dc = gD(now) == SENT ? 0 : gD(now), pp = gP(now) == SENT ? 0 : gP(now);
  snprintf(cv, sizeof(cv), "%d\xF7" "C", tc); snprintf(cv2, sizeof(cv2), "%d\xF7" "F", tc * 9 / 5 + 32);
  sparkRow(0, "temp", gT, cv, cv2, gTheme.warn);
  snprintf(cv, sizeof(cv), "%d\xF7" "C", dc); snprintf(cv2, sizeof(cv2), "%d\xF7" "F", dc * 9 / 5 + 32);
  sparkRow(1, "dewpt", gD, cv, cv2, gTheme.accent);
  snprintf(cv, sizeof(cv), "%d%%", gC(now) == SENT ? 0 : gC(now));
  sparkRow(2, "cloud", gC, cv, "", gTheme.fg);
  snprintf(cv, sizeof(cv), "%dhPa", pp); snprintf(cv2, sizeof(cv2), "%.2fin", pp * 0.02953f);
  sparkRow(3, "press", gP, cv, cv2, gTheme.ok);

  // Conclusion over the displayed window (now -> end), so it matches the graph.
  int Pe = SENT, Ce = SENT;
  for (int i = now + span; i > now; --i) { if (gP(i) != SENT) { Pe = gP(i); break; } }
  for (int i = now + span; i > now; --i) { if (gC(i) != SENT) { Ce = gC(i); break; } }
  int dP = (Pe != SENT && gP(now) != SENT) ? Pe - gP(now) : 0;
  int dC = (Ce != SENT && gC(now) != SENT) ? Ce - gC(now) : 0;
  int spread = (gT(now) != SENT && gD(now) != SENT) ? gT(now) - gD(now) : 99;
  String concl = dP <= -2 ? "pressure falling - unsettled"
               : dP >= 2  ? "pressure rising - improving" : "pressure steady";
  if (spread <= 2)      concl += "; fog/low-cld risk";
  else if (dC >= 25)    concl += "; clouding over";
  else if (dC <= -25)   concl += "; clearing";
  g.setTextDatum(textdatum_t::bottom_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString(concl.substring(0, (cw - 8) / 6), 4, cy0 + ch - 2);
}

// Station wind barb: a staff in the FROM direction with barbs (full = 10 kt,
// half = 5 kt, pennant = 50 kt), standard meteorological notation.
static void windBarb(lgfx::LovyanGFX& g, int x, int y, int wdir, int wspd, Color col) {
  if (wspd <= 0 || wdir < 0) return;
  const double D2R = 3.14159265358979323846 / 180.0, a = wdir * D2R;
  double dx = sin(a), dy = -cos(a);                 // staff direction (toward where wind comes from)
  double px = cos(a), py = sin(a);                  // perpendicular (barbs hang to one side)
  const int staff = 15;
  int ex = x + (int)round(staff * dx), ey = y + (int)round(staff * dy);
  g.drawLine(x, y, ex, ey, col);                    // staff
  int kt = ((wspd + 2) / 5) * 5;                    // round to nearest 5
  int pen = kt / 50; kt -= pen * 50;
  int full = kt / 10, half = (kt % 10) >= 5 ? 1 : 0;
  double bx = ex, by = ey;                          // build barbs from the upwind end inward
  const double step = 3.5, blen = 6.0;
  for (int i = 0; i < pen; ++i) {                   // pennant (triangle) per 50 kt
    g.fillTriangle((int)bx, (int)by, (int)(bx - step * 1.6 * dx), (int)(by - step * 1.6 * dy),
                   (int)(bx + blen * px), (int)(by + blen * py), col);
    bx -= step * 2 * dx; by -= step * 2 * dy;
  }
  for (int i = 0; i < full; ++i) {                  // full barb per 10 kt
    g.drawLine((int)bx, (int)by, (int)(bx + blen * px), (int)(by + blen * py), col);
    bx -= step * dx; by -= step * dy;
  }
  if (half) {                                       // half barb for the trailing 5 kt
    if (full == 0 && pen == 0) { bx -= step * dx; by -= step * dy; }   // hold a half off the very tip
    g.drawLine((int)bx, (int)by, (int)(bx + blen * 0.55 * px), (int)(by + blen * 0.55 * py), col);
  }
}

// Makeshift synoptic map from major-airport METARs: each station plotted on a
// region coastline, coloured by sea-level pressure (blue high / red low) with the
// max/min flagged H/L, or by cloud cover (top-left badge toggles). Not a real WPC
// frontal analysis, but a recognisable high/low pattern from a feed we already use.
void PageAviation::drawPressure(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  const auto& pts = _pmap.points();
  bool world = _pmap.worldwide();

  // Layers (top-left badge cycles): category / cloud / wind / inHg. The dot is ALWAYS
  // coloured by pressure (inHg: blue hi / grey normal / red low) and the ICAO text by
  // flight category; at 1x the layer picks the extra value shown next to each dot.
  // Zoomed in (>=2.6x) a richer 2-3 line readout shows regardless of layer.
  int scope = _pmap.scope();
  const char* ml = _presMode == 0 ? "cat" : _presMode == 1 ? "cloud" : _presMode == 2 ? "wind" : "inHg";
  const char* sc = scope == 0 ? "200mi" : scope == 1 ? "US" : "world";
  bool zoom2 = _pZoomCur > 1.6f;                               // 2.6x+: multi-line readout
  bool zoom3 = _pZoomCur > 3.5f;                               // 4.5x+: + wind kt line
  g.setTextDatum(textdatum_t::top_left); g.setTextSize(1);
  g.fillRect(2, cy0 + 2, 40, 12, gTheme.grid);                 // layer badge (tap: cat/cloud/wind/inHg)
  g.setTextColor(gTheme.fg, gTheme.grid); g.drawString(ml, 6, cy0 + 3);
  g.setTextColor(gTheme.fg, gTheme.bg);
  char zlbl[20]; snprintf(zlbl, sizeof(zlbl), "[tap: zoom %.1fx]", _pZoomCur);
  g.drawString(zlbl, 46, cy0 + 3);
  g.fillRect(cw - 82, cy0 + 2, 30, 12, gTheme.grid);           // recenter badge (tap: home @1x)
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.ok, gTheme.grid); g.drawString("home", cw - 80, cy0 + 3);
  g.fillRect(cw - 48, cy0 + 2, 46, 12, gTheme.grid);           // scope badge (tap: zoom 200mi/US/world)
  g.setTextDatum(textdatum_t::top_right);
  g.setTextColor(gTheme.fg, gTheme.grid); g.drawString(sc, cw - 4, cy0 + 3);

  double w0, w1, a0, a1; _pmap.bbox(w0, w1, a0, a1);            // bbox from the active scope
  // Render the UNION of this scope's points and the shared per-airport pool in the
  // box (so the map shows whatever any feed has fetched for this area).
  std::vector<PressurePt> merged(pts.begin(), pts.end());
  { std::vector<const MetarRec*> sr; MetarStore::instance().inBox(a0, w0, a1, w1, sr);
    for (auto* r : sr) {
      PressurePt* ex = nullptr; for (auto& q : merged) if (q.icao == r->icao) { ex = &q; break; }
      if (ex) { if (ex->cat.length() == 0) ex->cat = r->cat; }     // enrich a pmap pt with flight category
      else if (r->hpa >= 0 || r->cat.length()) {                   // add a METAR-only station (category-only ok)
        PressurePt q; q.icao = r->icao; q.lat = r->lat; q.lon = r->lon;
        q.hpa = r->hpa; q.cloud = r->cloud; q.wdir = r->wdir; q.wspd = r->wspd; q.cat = r->cat;
        merged.push_back(q); }
    } }
  int mx = 2, my = cy0 + 16, mw = cw - 4, mh = ch - 16 - mapBottomH();
  float zs = _pZoomCur;                                         // discrete tap-to-zoom factor about the focus
  auto SX = [&](double lon) { int x = mx + (int)((lon - w0) / (w1 - w0) * mw); return _pFx + (int)(zs * (x - _pFx)); };
  auto SY = [&](double lat) { int y = my + (int)((a1 - lat) / (a1 - a0) * mh); return _pFy + (int)(zs * (y - _pFy)); };
  g.drawRect(mx, my, mw, mh, gTheme.grid);
  // Coastlines + borders, then (regional view only) state/province lines, both
  // clipped to the bbox. Natural Earth coords are in 0.1-degree units.
  auto drawLines = [&](const CoastPt* arr, int n, Color col) {
    for (int i = 1; i < n; ++i) {
      double alon = arr[i - 1].lon / 10.0, alat = arr[i - 1].lat / 10.0;
      double blon = arr[i].lon / 10.0,     blat = arr[i].lat / 10.0;
      if (arr[i - 1].lon == 9999 || arr[i].lon == 9999 || fabs(alon - blon) > 180) continue;
      if (alon < w0 || alon > w1 || blon < w0 || blon > w1 || alat < a0 || alat > a1 || blat < a0 || blat > a1) continue;
      g.drawLine(SX(alon), SY(alat), SX(blon), SY(blat), col);
    }
  };
  drawLines(kCoastline, kCoastlineCount, gTheme.dim);
  if (!world) drawLines(kStateLines, kStateLinesCount, gTheme.grid);   // state lines only at regional zoom

  if (merged.empty()) {                                         // outline drawn; no station data yet
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(_pmap.status() == ProviderStatus::Error ? "feed down" : String("loading ") + sc + "...",
                 cw / 2, cy0 + ch / 2);
    g.setTextDatum(textdatum_t::top_left);
    return;
  }

  // Station shown in the bottom strip: the user's pick, else the closest field to the
  // observer (so a METAR is always on screen). The matching dot gets the ring.
  String tgt = _mapSelIcao;
  if (!tgt.length()) { const Metar* b = nullptr;
    for (const auto& s : _wx.stations()) if (!b || s.distNm < b->distNm) b = &s;
    if (b) tgt = b->icao; }
  _mapDotN = 0;                                               // cache this frame's dots for tap-to-select
  for (size_t i = 0; i < merged.size(); ++i) {
    const PressurePt& p = merged[i];
    int x = SX(p.lon), y = SY(p.lat);
    if (x < mx || x > mx + mw || y < my || y > my + mh) continue;
    Color cc  = p.cloud < 0 ? gTheme.dim : p.cloud < 30 ? gTheme.ok : p.cloud < 70 ? gTheme.accent : gTheme.warn; // cloud band (dim=unknown)
    String cld = p.cloud >= 0 ? String(p.cloud) + "%" : String("--");                    // cloud cover (-- = unknown)
    Color pc  = p.hpa >= 1019 ? gTheme.accent : p.hpa <= 1009 ? gTheme.warn : gTheme.fg; // inHg band -> the dot
    Color ctc = p.cat.length() ? catColor(p.cat) : gTheme.dim;                           // flight category -> id text
    bool seld = (tgt.length() && p.icao == tgt);
    if (!seld) {                                              // declutter: thin dots crowding an already-
      int minPx = _pZoomCur > 1.6f ? 15 : 26;                // drawn one (the 200mi view is dense); zooming
      bool crowd = false;                                    // spreads them so more reveal. Selected/nearest
      for (int k = 0; k < _mapDotN; ++k) {                   // is exempt so it always shows.
        int dx = x - _mapDotX[k], dy = y - _mapDotY[k];
        if (dx * dx + dy * dy < minPx * minPx) { crowd = true; break; }
      }
      if (crowd) continue;
    }
    g.fillCircle(x, y, seld ? 3 : 2, pc);                     // dot ALWAYS coloured by pressure (inHg)
    if (seld) g.drawCircle(x, y, 5, gTheme.fg);              // selection ring
    if (_mapDotN < kMapDots) { _mapDotIcao[_mapDotN] = p.icao; _mapDotX[_mapDotN] = (int16_t)x; _mapDotY[_mapDotN] = (int16_t)y; _mapDotN++; }
    String id = p.icao; if (id.length() == 4 && id[0] == 'K') id = id.substring(1);      // drop US K
    char inhg[8] = ""; if (p.hpa > 0) snprintf(inhg, sizeof(inhg), "%.2f", p.hpa * 0.02953f);
    g.setTextDatum(textdatum_t::top_left);
    if (zoom2) {                                              // 2.6x+: wind barb + multi-line readout near the dot
      windBarb(g, x, y, p.wdir, p.wspd, gTheme.fg);
      g.setTextColor(ctc, gTheme.bg); g.drawString(id, x + 5, y - 10);                              // line 1: id (cat) ...
      g.setTextColor(cc, gTheme.bg);  g.drawString(cld, x + 5 + ((int)id.length() + 1) * 6, y - 10);  // ... + cloud%
      if (inhg[0]) { g.setTextColor(pc, gTheme.bg); g.drawString(inhg, x + 5, y - 1); }            // line 2: inHg (pressure colour)
      if (zoom3 && p.wspd >= 0) {                                                                   // line 3 (4.5x+): wind kt
        g.setTextColor(gTheme.dim, gTheme.bg);
        g.drawString(p.wspd == 0 ? String("calm") : String(p.wspd) + "kt", x + 5, y + 8);
      }
    } else {                                                  // 1x: id (cat colour) + the active layer's value
      g.setTextColor(ctc, gTheme.bg); g.drawString(id, x + 4, y - 4);
      int lx = x + 4 + ((int)id.length() + 1) * 6;
      if (_presMode == 0) { if (p.cat.length()) g.drawString(p.cat, lx, y - 4); }                   // category (id colour shows it)
      else if (_presMode == 1) { g.setTextColor(cc, gTheme.bg); g.drawString(cld, lx, y - 4); }
      else if (_presMode == 2) { windBarb(g, x, y, p.wdir, p.wspd, gTheme.fg);
        if (p.wspd >= 0) { g.setTextColor(gTheme.dim, gTheme.bg);
          g.drawString(p.wspd == 0 ? String("calm") : String(p.wspd) + "kt", lx, y - 4); } }
      else { if (inhg[0]) { g.setTextColor(pc, gTheme.bg); g.drawString(inhg, lx, y - 4); } }       // inHg
    }
  }
  // Observer location: green circle + black centre dot (matches the Satellites/Launches maps).
  if (_loc.active().valid) {
    double ox = _loc.active().lon, oy = _loc.active().lat;
    if (ox >= w0 && ox <= w1 && oy >= a0 && oy <= a1) {
      int x = SX(ox), y = SY(oy);
      g.fillCircle(x, y, 4, gTheme.ok); g.fillCircle(x, y, 1, 0x0000);
    }
  }
  // Bottom strip: decoded METAR for the selected/closest field, sourced from the unified
  // MetarStore so ANY plotted station shows data; the nearby-feed record adds raw text +
  // dewpoint where it has them. With no fields loaded at all, a one-line mode legend.
  int by = my + mh + 2;
  const MetarRec* mr = nullptr;
  if (tgt.length()) for (auto& e : MetarStore::instance().all()) if (e.icao == tgt) { mr = &e; break; }
  const Metar* s = tgt.length() ? findStation(tgt) : nullptr;       // nearby feed: carries raw + dewpoint
  if (mr || s) {
    String cat = (s && s->cat.length()) ? s->cat : mr ? mr->cat : String();
    float  vis = (s && s->visSm >= 0) ? s->visSm : mr ? mr->visSm : -1;
    int    cig = (s && s->ceilingFt >= 0) ? s->ceilingFt : mr ? mr->ceilingFt : -1;
    int    tC, dC;
    if (s && s->tempC > -999) { tC = s->tempC; dC = s->dewpC; }
    else if (mr)              { tC = mr->tempC; dC = mr->dewpC; }
    else                      { tC = dC = -999; }
    int wd = s ? s->wdir : mr ? mr->wdir : -1;
    int ws = s ? s->wspd : mr ? mr->wspd : -1;
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(cat.length() ? catColor(cat) : gTheme.fg, gTheme.bg);
    g.drawString(tgt + " " + (cat.length() ? cat : String("--"))
                 + (_mapSelIcao.length() ? "" : "  (nearest)"), 4, by);
    String det = String("vis") + (vis >= 0 ? String(vis, 0) : String("--"))
               + (cig >= 0 ? "  cig" + String(cig) : String("  cig none"));
    if (tC > -999) det += String("  ") + tC + "/" + dC + "C";
    det += String("  w") + (ws == 0 ? String("calm")
                            : (wd < 0 ? String("VRB") : String(wd)) + "@" + ws);
    g.setTextDatum(textdatum_t::top_right); g.setTextColor(gTheme.fg, gTheme.bg);
    g.drawString(det, cw - 4, by);
    g.setTextDatum(textdatum_t::top_left);
    if (s && s->raw.length()) drawWrapped(g, s->raw, 4, by + 11, (cw - 8) / 6, 2, gTheme.dim);
    else { g.setTextColor(gTheme.dim, gTheme.bg);
           g.drawString("(decoded from area feed - no raw METAR here)", 4, by + 11); }
  } else {
    g.setTextDatum(textdatum_t::bottom_left); g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString("dot=inHg (blue hi/red lo)  id=flight cat  layer " + String(ml), 4, cy0 + ch - 1);
  }
}
