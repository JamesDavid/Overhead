#include "PageSolarSystem.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../services/TimeService.h"
#include "../services/LocationService.h"
#include "../services/Settings.h"
#include "../astro/Moons.h"
#include <math.h>
#include <time.h>

using astro::Planet;

static const char* kAbbrev[9] = { "Su", "Mo", "Me", "Ve", "Ma", "Ju", "Sa", "Ur", "Ne" };

static const char* moonPhaseName(double deg) {
  if (deg <  22.5) return "New";
  if (deg <  67.5) return "Waxing Cres";
  if (deg < 112.5) return "First Qtr";
  if (deg < 157.5) return "Waxing Gibb";
  if (deg < 202.5) return "Full";
  if (deg < 247.5) return "Waning Gibb";
  if (deg < 292.5) return "Last Qtr";
  if (deg < 337.5) return "Waning Cres";
  return "New";
}

// Small drawn moon-phase disk (waxing lit on the right). phaseDeg: 0 new..180 full..360.
static void drawMoonPhase(LGFX& g, int cx, int cy, int r, double phaseDeg) {
  const double D2R = 3.14159265358979323846 / 180.0;
  double c = cos(phaseDeg * D2R);
  g.fillCircle(cx, cy, r, gTheme.grid);                  // dark disk
  for (int y = -r; y <= r; ++y) {
    int w = (int)round(sqrt((double)(r * r - y * y)));
    int xt = (int)round(w * c);                          // terminator x on this scanline
    int x1, x2;
    if (phaseDeg < 180) { x1 = xt;  x2 = w; }            // waxing: right limb lit
    else                { x1 = -w;  x2 = -xt; }          // waning: left limb lit
    if (x2 >= x1) g.drawFastHLine(cx + x1, cy + y, x2 - x1 + 1, gTheme.fg);
  }
  g.drawCircle(cx, cy, r, gTheme.dim);
}

// Build the orbit-view body list: planets for the current scope (inner = Me..Ma,
// all = Me..Pluto) plus the minor bodies enabled in settings ("orreryBodies" CSV).
// In the inner scope only close minors (a <= 1.8 AU, e.g. Starman) are shown.
int PageSolarSystem::buildOrbit(OrbBody* out, int maxN) {
  int n = 0;
  int planetN = _orbScope == 0 ? 4 : _orbScope == 1 ? 6 : astro::kOrbitBodies;  // inner/mid/all
  for (int i = 0; i < planetN && n < maxN; ++i) out[n++] = { false, i };
  double maxAu = astro::orbitMeanAu(planetN - 1);          // outermost planet in scope
  String en = _settings.getString("orreryBodies", "Roadster,Psyche,Ceres,Vesta");
  for (int j = 0; j < astro::orbitMinorCount() && n < maxN; ++j) {
    if (en.indexOf(astro::orbitMinorName(j)) < 0) continue;
    if (astro::orbitMinorAu(j) > maxAu) continue;          // only minors inside the scope
    out[n++] = { true, j };
  }
  return n;
}
int PageSolarSystem::orbitVisibleCount() { OrbBody t[kMaxOrb]; return buildOrbit(t, kMaxOrb); }

bool PageSolarSystem::visible(int i) const {
  if (_filter == 1) return _st[i].above;
  if (_filter == 2) return i < 7;        // naked-eye: drop Uranus/Neptune
  return true;
}

void PageSolarSystem::recompute() {
  double jd = _time.julianDate();
  double lat = _loc.active().lat, lon = _loc.active().lon;
  for (int i = 0; i < kN; ++i)
    _st[i] = astro::planetState((Planet)i, jd, lat, lon);
}

void PageSolarSystem::onTouch(App& app, int x, int y) {
  int third = app.contentW() / 3;
  // Centre tap cycles sky-dome -> orbits -> moons & rings.
  if (x >= third && x <= 2 * third) { _view = (_view + 1) % 3; _dirty = true; return; }
  // Bottom-left badge cycles the filter (sky-dome view only).
  if (_view == 0 && x <= 80 && y >= app.contentH() - 20) {
    _filter = (_filter + 1) % 3; _settings.set("ssShowFilter", (long)_filter); _settings.save();
    _dirty = true; return;
  }
  if (_view == 1) {                         // orbits
    if (x > app.contentW() - 52 && y >= app.contentH() - 16) {   // bottom-right: inner/all
      _orbScope = (_orbScope + 1) % 3;                    // inner -> mid -> all
      _settings.set("orbScope", (long)_orbScope); _settings.save();
      int cnt = orbitVisibleCount();
      if (_orbSel >= cnt) _orbSel = cnt - 1;
      _dirty = true; return;
    }
    int cnt = orbitVisibleCount();
    if (x < third) _orbSel = (_orbSel - 1 + cnt) % cnt;          // step visible bodies
    else           _orbSel = (_orbSel + 1) % cnt;
  } else {                                  // sky-dome: step visible bodies
    if (x < third)          { do { _sel = (_sel - 1 + kN) % kN; } while (!visible(_sel) && _filter); }
    else if (x > 2 * third) { do { _sel = (_sel + 1) % kN; } while (!visible(_sel) && _filter); }
  }
  _dirty = true;
}

bool PageSolarSystem::autoAdvance(App&) {
  bool cycled = false;
  if (_view == 0) {                         // sky-dome: tour the visible bodies
    int vis = 0; for (int i = 0; i < kN; ++i) if (visible(i)) vis++;
    if (vis == 0) { _view = 1; _tourN = 0; _orbSel = 0; _dirty = true; return false; }
    int g = 0; do { _sel = (_sel + 1) % kN; } while (!visible(_sel) && ++g < kN);
    if (++_tourN >= vis) { _tourN = 0; _view = 1; _orbSel = 0; }   // toured all -> orbits
  } else if (_view == 1) {                  // orbits: tour the visible bodies
    int cnt = orbitVisibleCount();
    _orbSel = (_orbSel + 1) % cnt;
    if (++_tourN >= cnt) { _tourN = 0; _view = 2; }               // orbits done -> moons
  } else {                                  // moons & rings: one dwell -> full cycle
    _view = 0; cycled = true;
  }
  _dirty = true;
  return cycled;
}

void PageSolarSystem::onEnter(App&) {
  _dirty = true;
  _filter   = (int)_settings.getInt("ssShowFilter", 1);   // persisted show-filter + orbit scope
  _orbScope = (int)_settings.getInt("orbScope", 2);
}

void PageSolarSystem::tick(App& app, uint32_t nowMs) {
  // Positions drift over minutes — recompute/redraw on change or every 30 s.
  if (!_dirty && nowMs - _lastDraw < 30000) return;
  _dirty = false;
  _lastDraw = nowMs;
  if (_time.synced() && _loc.active().valid) recompute();
  draw(app);
}

void PageSolarSystem::draw(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), ch = app.contentH(), cy0 = app.contentY();
  g.fillRect(0, cy0, cw, ch, gTheme.bg);   // slow cadence -> full clear is fine

  if (!_time.synced() || !_loc.active().valid) {
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(_time.synced() ? "no location" : "waiting for time sync...", cw / 2, cy0 + ch / 2);
    return;
  }

  if (_view == 1) { drawOrbit(app); return; }
  if (_view == 2) { drawMoons(app); return; }

  // --- Horizon half-dome (top ~55%) ---
  const int domeH = ch * 55 / 100;
  const int horY = cy0 + domeH;
  g.drawFastHLine(0, horY, cw, gTheme.grid);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.setTextDatum(textdatum_t::bottom_center);
  g.drawString("N", 1, horY - 1);
  g.drawString("E", cw / 4, horY - 1);
  g.drawString("S", cw / 2, horY - 1);
  g.drawString("W", cw * 3 / 4, horY - 1);

  for (int i = 0; i < kN; ++i) {
    if (!_st[i].above) continue;
    int x = (int)(_st[i].azDeg / 360.0 * cw);
    int y = horY - (int)(_st[i].elDeg / 90.0 * (domeH - 10)) - 4;
    Color c = (i == _sel) ? gTheme.ok : (i == 0 ? gTheme.warn : gTheme.accent);
    if (i == 1) drawMoonPhase(g, x, y, 4, astro::moonPhaseDeg(_time.julianDate()));  // Moon shows its phase
    else        g.fillCircle(x, y, (i == _sel) ? 3 : 2, c);
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextColor(c, gTheme.bg);
    g.drawString(kAbbrev[i], x + 5, y);
  }

  // --- List (below the horizon) ---
  int ly = horY + 4;
  g.setTextSize(1);
  for (int i = 0; i < kN && ly < cy0 + ch - 14; ++i) {
    if (!visible(i)) continue;
    Color c = (i == _sel) ? gTheme.ok : gTheme.fg;
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(c, gTheme.bg);
    String row = String(astro::planetName((Planet)i));
    if (i == 1) row += String("  ") + (int)astro::moonIlluminationPct(_time.julianDate()) + "% " + moonPhaseName(astro::moonPhaseDeg(_time.julianDate()));
    g.drawString(row, 6, ly);
    g.setTextDatum(textdatum_t::top_right);
    g.setTextColor(_st[i].above ? gTheme.fg : gTheme.dim, gTheme.bg);
    char b[28];
    if (_st[i].above) snprintf(b, sizeof(b), "el %d  az %d", (int)round(_st[i].elDeg), (int)round(_st[i].azDeg));
    else              snprintf(b, sizeof(b), "below");
    g.drawString(b, cw - 6, ly);
    ly += 13;
  }

  // Filter badge (bottom-left) + orbit-view hint (bottom-right).
  const char* fl = _filter == 0 ? "all" : _filter == 1 ? "up" : "eye";
  int by = cy0 + ch - 16;
  g.fillRect(4, by, 72, 14, gTheme.grid);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(gTheme.fg, gTheme.grid);
  g.drawString(String("show ") + fl, 8, by + 7);
  g.setTextDatum(textdatum_t::middle_right);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString("tap mid: orbits", cw - 6, by + 7);
}

// Top-down orrery: Sun at centre, sqrt-scaled orbit rings (so the inner planets
// aren't crushed by Pluto's 39 AU), each body at its live heliocentric longitude.
void PageSolarSystem::drawOrbit(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), ch = app.contentH(), cy0 = app.contentY();
  const double D2R = 3.14159265358979323846 / 180.0;
  double jd = _time.julianDate();

  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.drawString("Orbits (top-down)  [tap mid: moons]", 4, cy0 + 1);

  int cx = cw / 2, cy = cy0 + (ch - 14) / 2 + 12;
  int maxR = min(cw / 2, (ch - 26) / 2) - 8;
  OrbBody bodies[kMaxOrb];
  int count = buildOrbit(bodies, kMaxOrb);
  if (count == 0) return;
  if (_orbSel >= count) _orbSel = count - 1;
  auto bAu = [&](OrbBody b) { return b.minor ? astro::orbitMinorAu(b.idx) : astro::orbitMeanAu(b.idx); };
  double maxAu = 0;                                              // outermost ring shown
  for (int i = 0; i < count; ++i) maxAu = max(maxAu, bAu(bodies[i]));
  auto rad = [&](double au) { return (int)round(sqrt(au / maxAu) * maxR); };

  g.fillCircle(cx, cy, 3, gTheme.warn);                          // Sun

  astro::HelioPos sel{}; const char* selName = "?";
  for (int i = 0; i < count; ++i) {
    OrbBody b = bodies[i];
    int rr = rad(bAu(b));
    if (b.minor) for (int t = 0; t < 360; t += 18) g.drawPixel(cx + (int)round(rr * cosf(t * D2R)), cy - (int)round(rr * sinf(t * D2R)), gTheme.grid); // dashed orbit
    else         g.drawCircle(cx, cy, rr, gTheme.grid);          // orbit ring
    astro::HelioPos hp = b.minor ? astro::orbitMinorPos(b.idx, jd) : astro::heliocentricBody(b.idx, jd);
    double a = hp.lonDeg * D2R;
    int pxp = cx + (int)round(rr * cos(a));
    int pyp = cy - (int)round(rr * sin(a));
    bool s = (i == _orbSel);
    Color c = s ? gTheme.ok : b.minor ? gTheme.warn : (b.idx == 2 ? gTheme.accent : gTheme.fg);  // minor=warn, Earth=accent
    g.fillCircle(pxp, pyp, s ? 3 : 2, c);
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextColor(c, gTheme.bg);
    g.drawString(b.minor ? astro::orbitMinorSym(b.idx) : astro::orbitBodyName(b.idx), pxp + 4, pyp);
    if (s) { sel = hp; selName = b.minor ? astro::orbitMinorName(b.idx) : astro::orbitBodyName(b.idx); }
  }

  // Selected-body readout (bottom-left) + inner/all scope badge (bottom-right).
  g.setTextDatum(textdatum_t::bottom_left);
  g.setTextColor(gTheme.ok, gTheme.bg);
  char b[44];
  snprintf(b, sizeof(b), "%s  %.2f AU  lon %d", selName, sel.rAu, (int)round(sel.lonDeg));
  g.drawString(b, 4, cy0 + ch - 2);
  int by = cy0 + ch - 15;
  g.fillRect(cw - 50, by, 48, 14, gTheme.grid);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(gTheme.fg, gTheme.grid);
  static const char* kScope[] = {"inner", "mid", "all"};
  g.drawString(kScope[_orbScope], cw - 26, by + 7);
}

// Telescopic preview: Jupiter's Galilean moons (apparent E-W line) and Saturn's
// rings (opening angle), with each planet's up/below state.
void PageSolarSystem::drawMoons(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY();
  const double D2R = 3.14159265358979323846 / 180.0;
  double jd = _time.julianDate();
  int cx = cw / 2;
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.drawString("Moons & rings  [tap mid: sky]", 4, cy0 + 1);

  // --- Jupiter + Galilean moons (Io/Europa/Ganymede/Callisto strung along the equator) ---
  int jy = cy0 + 64;
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString(String("Jupiter  ") + (_st[5].above ? "up" : "below") + "  el " + (int)round(_st[5].elDeg) + "\xF7", 4, cy0 + 18);
  double mx[4]; astro::galileanMoons(jd, mx);
  const double jscale = 5.0;                       // px per Jupiter radius
  g.drawFastHLine(cx - 140, jy, 280, gTheme.grid);
  g.fillCircle(cx, jy, 5, gTheme.warn);            // Jupiter
  for (int i = 0; i < 4; ++i) {
    int x = cx + (int)round(mx[i] * jscale);
    if (x < 2 || x > cw - 2) continue;
    g.fillCircle(x, jy, 2, gTheme.accent);
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(astro::galileanSym(i), x - 5, jy + ((i & 1) ? 6 : -14));   // alternate up/down
  }

  // --- Saturn + rings ---
  int sy = cy0 + 158;
  double B = astro::saturnRingTiltDeg(jd);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString(String("Saturn  ") + (_st[6].above ? "up" : "below") + "  rings " + (int)round(fabs(B)) + "\xF7 open", 4, sy - 44);
  int rMaj = 28, rMin = (int)round(rMaj * fabs(sin(B * D2R)));
  if (rMin < 1) rMin = 1;
  g.fillCircle(cx, sy, 7, gTheme.warn);                          // planet disk
  g.drawEllipse(cx, sy, rMaj, rMin, gTheme.fg);                  // outer ring edge
  if (rMin >= 3) g.drawEllipse(cx, sy, (int)(rMaj * 0.6), (int)(rMin * 0.6), gTheme.dim);  // inner edge / Cassini hint
}
