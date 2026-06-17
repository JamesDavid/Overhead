#include "PageStarMap.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../services/TimeService.h"
#include "../services/LocationService.h"
#include "../assets/StarCatalog.h"
#include "../astro/Time.h"
#include "../astro/Coords.h"
#include "../astro/SolarSystem.h"
#include <math.h>
#include <string.h>

// Constellations for the zoom tour: name + up to 8 member stars (must match catalog
// names). Used for the framing centroid + the star labels shown when zoomed in.
struct Constellation { const char* name; const char* stars[8]; };
static const Constellation kCons[] = {
  {"Orion",       {"Betelgeuse","Bellatrix","Mintaka","Alnilam","Alnitak","Rigel","Saiph"}},
  {"Ursa Major",  {"Dubhe","Merak","Phecda","Megrez","Alioth","Mizar","Alkaid"}},
  {"Cassiopeia",  {"Caph","Schedar","Navi","Ruchbah","Segin"}},
  {"Cygnus",      {"Deneb","Sadr","Gienah","Fawaris","Albireo"}},
  {"Lyra",        {"Vega","Sheliak","Sulafat"}},
  {"Leo",         {"Regulus","Algieba","Zosma","Denebola","Chertan"}},
  {"Bootes",      {"Arcturus","Izar","Seginus","Nekkar","Muphrid"}},
  {"Scorpius",    {"Antares","Dschubba","Acrab","Shaula","Sargas","Lesath"}},
  {"Gemini",      {"Castor","Pollux","Alhena"}},
  {"Canis Major", {"Sirius","Mirzam","Wezen","Adhara"}},
  {"Crux",        {"Acrux","Mimosa","Gacrux","Imai"}},
  {"Pegasus",     {"Markab","Scheat","Algenib","Alpheratz"}},
  {"Aquila",      {"Altair","Tarazed","Alshain"}},
  {"Auriga",      {"Capella","Menkalinan","Mahasim","Elnath"}},
};
static const int kConCount = sizeof(kCons) / sizeof(kCons[0]);

static const Star* findStar(const char* n) {
  for (int k = 0; k < kStarCount; ++k) if (!strcmp(kStars[k].name, n)) return &kStars[k];
  return nullptr;
}
static inline float smooth(float x) { x = x < 0 ? 0 : x > 1 ? 1 : x; return x * x * (3 - 2 * x); }

// Project a star to screen (azimuthal: zenith centre, horizon edge). Returns
// false if below the horizon.
static bool project(const Star& s, double jd, double latRad, double lst,
                    int cx, int cy, int R, int& sx, int& sy, float& alt) {
  astro::Equatorial eq{ s.raHours * 15.0 * astro::DEG2RAD, s.decDeg * astro::DEG2RAD };
  astro::Horizontal h = astro::equatorialToHorizontal(eq, latRad, lst);
  alt = h.altRad * astro::RAD2DEG;
  if (alt <= 0) return false;
  double rr = R * (90.0 - alt) / 90.0;
  sx = cx + (int)round(rr * sin(h.azRad));
  sy = cy - (int)round(rr * cos(h.azRad));
  return true;
}

bool PageStarMap::starInCon(int con, const char* name) const {
  if (con < 0 || con >= kConCount) return false;
  for (const char* nm : kCons[con].stars) { if (!nm) break; if (!strcmp(nm, name)) return true; }
  return false;
}

// Screen centroid + count of a constellation's above-horizon member stars.
bool PageStarMap::conFocus(App& app, int con, int& fx, int& fy, int& count) {
  count = 0; fx = app.contentW() / 2; fy = app.contentY() + app.contentH() / 2;
  if (con < 0 || con >= kConCount || !_time.synced() || !_loc.active().valid) return false;
  const int cw = app.contentW(), ch = app.contentH(), cy0 = app.contentY();
  int R = min(cw, ch) / 2 - 8, cx = cw / 2, cy = cy0 + ch / 2;
  double jd = _time.julianDate(), latRad = _loc.active().lat * astro::DEG2RAD;
  double lst = astro::lstRad(jd, _loc.active().lon);
  long sxs = 0, sys = 0;
  for (const char* nm : kCons[con].stars) {
    if (!nm) break;
    const Star* s = findStar(nm); if (!s) continue;
    int px, py; float alt;
    if (project(*s, jd, latRad, lst, cx, cy, R, px, py, alt)) { sxs += px; sys += py; count++; }
  }
  if (count) { fx = (int)(sxs / count); fy = (int)(sys / count); return true; }
  return false;
}

int PageStarMap::nextVisibleCon(App& app, int from) {
  for (int i = 1; i <= kConCount; ++i) {
    int idx = (from + i) % kConCount, fx, fy, c;
    conFocus(app, idx, fx, fy, c);
    if (c >= 3) return idx;
  }
  return -1;
}

void PageStarMap::onTouch(App& app, int x, int y) {
  const int cw = app.contentW(), ch = app.contentH();
  if (x <= 80 && y >= ch - 20) {                   // bottom-left badge: mag limit
    _magLimit = (_magLimit >= 4.0f) ? 2.0f : _magLimit + 1.0f;
    _dirty = true; return;
  }
  if (x >= cw - 46 && y >= ch - 20) {              // bottom-right: SS overlay
    _showSS = !_showSS; _dirty = true; return;
  }
  if (y >= ch - 20 && x > cw / 2 - 26 && x < cw / 2 + 26) {  // bottom-centre: tour
    _tour = !_tour; _tourCon = -1; _t = 0; _dirty = true; return;
  }
  if (_tour) { _tour = false; _t = 0; _dirty = true; return; }  // any tap exits the tour
  int third = cw / 3;
  if (x >= third && x <= 2 * third) { _labels = !_labels; _dirty = true; }
}

bool PageStarMap::autoAdvance(App&) {
  // No discrete objects to select; "tour" = progressively reveal fainter stars
  // (mag 2 -> 3 -> 4), then signal a complete cycle so the rotation moves on.
  _magLimit += 1.0f;
  bool cycled = false;
  if (_magLimit > 4.0f) { _magLimit = 2.0f; cycled = true; }
  _dirty = true;
  return cycled;
}

// Drive the zoom animation: zoom-in -> hold (names shown) -> zoom-out -> next con.
void PageStarMap::updateTour(App& app, uint32_t nowMs) {
  const uint32_t IN = 1100, HOLD = 2600, OUT = 900;
  if (_tourCon < 0) {                              // (re)start: frame the first visible con
    _tourCon = nextVisibleCon(app, -1);
    if (_tourCon < 0) { _tour = false; _t = 0; return; }
    _phase = 0; _phaseMs = nowMs; _t = 0;
  }
  uint32_t el = nowMs - _phaseMs;
  if (_phase == 0) {                               // zoom in
    if (el >= IN) { _phase = 1; _phaseMs = nowMs; _t = 1; } else _t = smooth((float)el / IN);
  } else if (_phase == 1) {                        // hold, fully zoomed
    _t = 1;
    if (el >= HOLD) { _phase = 2; _phaseMs = nowMs; }
  } else {                                         // zoom out, then advance
    if (el >= OUT) {
      int nx = nextVisibleCon(app, _tourCon);
      if (nx < 0) { _tour = false; _t = 0; return; }
      _tourCon = nx; _phase = 0; _phaseMs = nowMs; _t = 0;
    } else _t = 1 - smooth((float)el / OUT);
  }
}

void PageStarMap::tick(App& app, uint32_t nowMs) {
  if (_tour) {                                     // animate at ~14 fps while touring
    updateTour(app, nowMs);
    if (!_tour) { _dirty = true; }                 // tour just ended -> redraw full sky
    else if (nowMs - _lastDraw < 70) return;
    _lastDraw = nowMs; _dirty = false; draw(app); return;
  }
  if (!_dirty && nowMs - _lastDraw < 30000) return; // sky rotates slowly
  _dirty = false; _lastDraw = nowMs;
  draw(app);
}

void PageStarMap::draw(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  g.fillRect(0, cy0, cw, ch, gTheme.bg);

  if (!_time.synced() || !_loc.active().valid) {
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(_time.synced() ? "no location" : "waiting for time sync...", cw / 2, cy0 + ch / 2);
    return;
  }

  int R = min(cw, ch) / 2 - 8;
  int cx = cw / 2, cy = cy0 + ch / 2;
  double jd = _time.julianDate();
  double latRad = _loc.active().lat * astro::DEG2RAD;
  double lst = astro::lstRad(jd, _loc.active().lon);

  // Zoom transform: display = C + s*(P - F) + (1-t)*(F - C). At t=0 identity; at
  // t=1 the focus F maps to centre C and the chart is magnified by Z.
  float t = (_tour && _tourCon >= 0) ? _t : 0.0f;
  int Fx = cx, Fy = cy;
  if (t > 0) { int c; conFocus(app, _tourCon, Fx, Fy, c); }
  const float Z = 3.6f;
  float s = 1 + t * (Z - 1);
  auto xf = [&](int px, int py, int& ox, int& oy) {
    ox = (int)(cx + s * (px - Fx) + (1 - t) * (Fx - cx));
    oy = (int)(cy + s * (py - Fy) + (1 - t) * (Fy - cy));
  };

  // Horizon circle + cardinals only when essentially un-zoomed (they don't transform).
  if (t < 0.05f) {
    g.drawCircle(cx, cy, R, gTheme.grid);
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.setTextDatum(textdatum_t::middle_center);
    g.drawString("N", cx, cy - R - 5); g.drawString("S", cx, cy + R + 5);
    g.drawString("E", cx + R + 5, cy); g.drawString("W", cx - R - 5, cy);
  }

  // Constellation lines (both endpoints above horizon).
  for (int i = 0; i < kStarLineCount; ++i) {
    const Star *a = nullptr, *b = nullptr;
    for (int k = 0; k < kStarCount; ++k) {
      if (!strcmp(kStars[k].name, kStarLines[i].a)) a = &kStars[k];
      if (!strcmp(kStars[k].name, kStarLines[i].b)) b = &kStars[k];
    }
    if (!a || !b) continue;
    int ax, ay, bx, by; float al, bl;
    if (project(*a, jd, latRad, lst, cx, cy, R, ax, ay, al) &&
        project(*b, jd, latRad, lst, cx, cy, R, bx, by, bl)) {
      int oax, oay, obx, oby; xf(ax, ay, oax, oay); xf(bx, by, obx, oby);
      g.drawLine(oax, oay, obx, oby, gTheme.grid);
    }
  }

  // Ecliptic — the path the Sun/Moon/planets travel along.
  if (_showSS) {
    const double eps = astro::DEG2RAD * 23.4393;
    int px = -1, py = -1;
    for (int dd = 0; dd <= 360; dd += 6) {
      double lam = dd * astro::DEG2RAD;
      astro::Equatorial eq{ atan2(sin(lam) * cos(eps), cos(lam)), asin(sin(eps) * sin(lam)) };
      astro::Horizontal h = astro::equatorialToHorizontal(eq, latRad, lst);
      double alt = h.altRad * astro::RAD2DEG;
      if (alt <= 0) { px = -1; continue; }
      double rr = R * (90.0 - alt) / 90.0;
      int sx0 = cx + (int)round(rr * sin(h.azRad)), sy0 = cy - (int)round(rr * cos(h.azRad));
      int sx, sy; xf(sx0, sy0, sx, sy);
      if (px >= 0) g.drawLine(px, py, sx, sy, gTheme.grid);
      px = sx; py = sy;
    }
  }

  // Stars (brightest first so labels favour them). During the tour, member stars of
  // the framed constellation are always drawn + named once mostly zoomed in.
  for (int k = 0; k < kStarCount; ++k) {
    const Star& s2 = kStars[k];
    bool member = _tour && starInCon(_tourCon, s2.name);
    if (s2.mag > _magLimit && !member) continue;
    int sx0, sy0; float alt;
    if (!project(s2, jd, latRad, lst, cx, cy, R, sx0, sy0, alt)) continue;
    int sx, sy; xf(sx0, sy0, sx, sy);
    if (sx < -10 || sx > cw + 10 || sy < cy0 - 10 || sy > cy0 + ch + 10) continue;
    int r = s2.mag < 0.5f ? 3 : s2.mag < 1.5f ? 2 : 1;
    g.fillCircle(sx, sy, member ? r + 1 : r, member ? gTheme.accent : gTheme.fg);
    bool showName = (member && t > 0.45f) || (!_tour && _labels && s2.mag <= 1.6f);
    if (showName) {
      g.setTextDatum(textdatum_t::bottom_left);
      g.setTextColor(member ? gTheme.fg : gTheme.dim, gTheme.bg);
      g.drawString(s2.name, sx + 4, sy - 1);
    }
  }

  // Sun / Moon / planets, projected the same way (azimuthal). Distinct colours.
  for (int i = 0; _showSS && i < 9; ++i) {
    astro::PlanetState p = astro::planetState((astro::Planet)i, jd, _loc.active().lat, _loc.active().lon);
    if (!p.above) continue;
    double rr = R * (90.0 - p.elDeg) / 90.0;
    int sx0 = cx + (int)round(rr * sin(p.azDeg * astro::DEG2RAD));
    int sy0 = cy - (int)round(rr * cos(p.azDeg * astro::DEG2RAD));
    int sx, sy; xf(sx0, sy0, sx, sy);
    Color c = (i == 0) ? gTheme.warn : (i == 1) ? gTheme.fg : gTheme.ok;   // Sun / Moon / planets
    g.fillCircle(sx, sy, i <= 1 ? 3 : 2, c);
    if (_labels && !_tour) {
      g.setTextDatum(textdatum_t::bottom_left);
      g.setTextColor(c, gTheme.bg);
      g.drawString(astro::planetName((astro::Planet)i), sx + 4, sy - 1);
    }
  }

  // Constellation name banner while zoomed.
  if (_tour && _tourCon >= 0 && t > 0.25f) {
    g.setTextDatum(textdatum_t::top_center);
    g.setTextColor(gTheme.accent, gTheme.bg);
    g.setTextSize(2);
    g.drawString(kCons[_tourCon].name, cw / 2, cy0 + 3);
    g.setTextSize(1);
  }

  // Badge: magnitude limit.
  int by = cy0 + ch - 16;
  g.fillRect(4, by, 78, 14, gTheme.grid);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(gTheme.fg, gTheme.grid);
  g.setTextSize(1);
  g.drawString(String("mag<=") + String(_magLimit, 0) + (_labels ? " +lbl" : ""), 8, by + 7);
  // Badge: tour toggle (bottom-centre).
  g.fillRect(cw / 2 - 24, by, 48, 14, gTheme.grid);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(_tour ? gTheme.ok : gTheme.dim, gTheme.grid);
  g.drawString(_tour ? "tour*" : "tour", cw / 2, by + 7);
  // Badge: solar-system overlay toggle (bottom-right).
  g.fillRect(cw - 46, by, 42, 14, gTheme.grid);
  g.setTextColor(_showSS ? gTheme.ok : gTheme.dim, gTheme.grid);
  g.drawString(_showSS ? "SS on" : "SS off", cw - 42, by + 7);
}
