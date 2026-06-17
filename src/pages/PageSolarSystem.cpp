#include "PageSolarSystem.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../services/TimeService.h"
#include "../services/LocationService.h"
#include "../services/Settings.h"
#include "../astro/Moons.h"
#include "../astro/Time.h"
#include "../assets/StarCatalog.h"
#include <math.h>
#include <string.h>
#include <time.h>

using astro::Planet;

static const char* kAbbrev[9] = { "Su", "Mo", "Me", "Ve", "Ma", "Ju", "Sa", "Ur", "Ne" };

// Parallactic angle q (deg): the tilt of the celestial-equator E-W direction
// relative to the local vertical (zenith up) at the body's position — i.e. how the
// face of a planet appears rotated when seen from the observer's latitude/sky.
// q = atan2(sin H, tan(lat)cos(dec) - sin(dec)cos H), H = LST - RA (hour angle).
static double parallacticDeg(const astro::PlanetState& st, double latDeg, double lstR) {
  const double D2R = 3.14159265358979323846 / 180.0;
  double H   = lstR - st.raDeg * D2R;
  double dec = st.decDeg * D2R, lat = latDeg * D2R;
  return atan2(sin(H), tan(lat) * cos(dec) - sin(dec) * cos(H)) / D2R;
}

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
  // Centre tap cycles sky-dome -> orbits -> Jupiter -> Saturn.
  if (x >= third && x <= 2 * third) { _view = (_view + 1) % kViews; _dirty = true; return; }
  // Bottom-left badge cycles the filter (sky-dome view only).
  if (_view == 0 && x <= 80 && y >= app.contentH() - 20) {
    _filter = (_filter + 1) % 3; _settings.set("ssShowFilter", (long)_filter); _settings.save();
    _dirty = true; return;
  }
  // Bottom-right badge toggles the star/constellation overlay (sky-dome view only).
  if (_view == 0 && x >= app.contentW() - 58 && y >= app.contentH() - 20) {
    _stars = !_stars; _settings.set("ssShowStars", _stars); _settings.save();
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
    if (++_tourN >= cnt) { _tourN = 0; _view = 2; }               // orbits done -> Jupiter
  } else if (_view == 2) {                  // Jupiter -> Saturn
    _view = 3;
  } else {                                  // Saturn: one dwell -> full cycle
    _view = 0; cycled = true;
  }
  _dirty = true;
  return cycled;
}

void PageSolarSystem::onEnter(App&) {
  _dirty = true;
  _filter   = (int)_settings.getInt("ssShowFilter", 1);   // persisted show-filter + orbit scope
  _orbScope = (int)_settings.getInt("orbScope", 2);
  _stars    = _settings.getBool("ssShowStars", false);
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

  if (_view == 1) { drawOrbit(app);   return; }
  if (_view == 2) { drawJupiter(app); return; }
  if (_view == 3) { drawSaturn(app);  return; }

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

  // Optional Star Map overlay: stars + constellation lines projected into the dome
  // (azimuth across the width, elevation up). Drawn before the planets so the
  // planet dots stay on top.
  if (_stars) {
    double jd = _time.julianDate();
    double latRad = _loc.active().lat * astro::DEG2RAD;
    double lst = astro::lstRad(jd, _loc.active().lon);
    auto proj = [&](float raH, float decD, int& sx, int& sy) -> bool {
      astro::Equatorial eq{ raH * 15.0 * astro::DEG2RAD, decD * astro::DEG2RAD };
      astro::Horizontal h = astro::equatorialToHorizontal(eq, latRad, lst);
      double el = h.altRad * astro::RAD2DEG;
      if (el <= 0) return false;
      double az = h.azRad * astro::RAD2DEG; if (az < 0) az += 360;
      sx = (int)(az / 360.0 * cw);
      sy = horY - (int)(el / 90.0 * (domeH - 10)) - 4;
      return true;
    };
    for (int i = 0; i < kStarLineCount; ++i) {       // constellation lines
      const Star *a = nullptr, *b = nullptr;
      for (int k = 0; k < kStarCount; ++k) {
        if (!strcmp(kStars[k].name, kStarLines[i].a)) a = &kStars[k];
        if (!strcmp(kStars[k].name, kStarLines[i].b)) b = &kStars[k];
      }
      if (!a || !b) continue;
      int ax, ay, bx, by;
      if (proj(a->raHours, a->decDeg, ax, ay) && proj(b->raHours, b->decDeg, bx, by) &&
          abs(ax - bx) < cw / 2)                      // skip the az wraparound seam
        g.drawLine(ax, ay, bx, by, gTheme.grid);
    }
    for (int k = 0; k < kStarCount; ++k) {            // star dots (brighter = bigger)
      if (kStars[k].mag > 3.5f) continue;
      int sx, sy;
      if (proj(kStars[k].raHours, kStars[k].decDeg, sx, sy))
        g.fillCircle(sx, sy, kStars[k].mag < 1.5f ? 2 : 1, gTheme.dim);
    }
  }

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
  // Bottom-right: star/constellation overlay toggle.
  g.fillRect(cw - 56, by, 52, 14, gTheme.grid);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(_stars ? gTheme.ok : gTheme.dim, gTheme.grid);
  g.drawString(_stars ? "stars on" : "stars off", cw - 30, by + 7);
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
  g.drawString("Orbits (top-down)  [tap mid: Jupiter]", 4, cy0 + 1);

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

// Telescopic Jupiter: the four Galilean moons strung along Jupiter's equator, the
// whole line rotated by the parallactic angle so it matches how the system actually
// appears in the observer's sky (zenith up) at this moment and latitude.
void PageSolarSystem::drawJupiter(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), ch = app.contentH(), cy0 = app.contentY();
  const double D2R = 3.14159265358979323846 / 180.0;
  double jd = _time.julianDate();
  double lstR = astro::lstRad(jd, _loc.active().lon);
  double q = parallacticDeg(_st[5], _loc.active().lat, lstR) * D2R;   // sky tilt
  double cq = cos(q), sq = sin(q);

  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.drawString("Jupiter - Galilean moons  [tap mid: Saturn]", 4, cy0 + 1);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString(String(_st[5].above ? "up" : "below horizon") + "   el " + (int)round(_st[5].elDeg)
               + "\xF7  az " + (int)round(_st[5].azDeg) + "\xF7", 4, cy0 + 16);

  int cx = cw / 2, cyc = cy0 + ch / 2;
  double mx[4]; astro::galileanMoons(jd, mx);
  const double jscale = 5.2;                        // px per Jupiter radius (Callisto ~26 Rj)
  // Equatorial line, rotated to the sky. (Screen y is down, so subtract the y-part.)
  double L = 145;
  g.drawLine(cx - (int)round(L * cq), cyc + (int)round(L * sq),
             cx + (int)round(L * cq), cyc - (int)round(L * sq), gTheme.grid);
  g.fillCircle(cx, cyc, 6, gTheme.warn);            // Jupiter
  for (int i = 0; i < 4; ++i) {
    double r = mx[i] * jscale;                      // signed offset along the equator
    int x = cx + (int)round(r * cq), y = cyc - (int)round(r * sq);
    if (x < 2 || x > cw - 2) continue;
    g.fillCircle(x, y, 2, gTheme.accent);
    g.setTextColor(gTheme.dim, gTheme.bg);
    // Stagger labels perpendicular to the line so they don't collide with it.
    int lx = x + (int)round(8 * sq), ly = y + (int)round(8 * cq) - 4;
    g.drawString(astro::galileanSym(i), lx - 5, ly);
  }
  // Legend (top-left): full names keyed to the graph syms, with each moon's live
  // elongation in Jupiter radii (E/W side of the disk). Drawn last to stay on top.
  g.setTextDatum(textdatum_t::top_left);
  int ly = cy0 + 32;
  for (int i = 0; i < 4; ++i) {
    g.setTextColor(gTheme.accent, gTheme.bg);
    char b[28];
    snprintf(b, sizeof(b), "%-9s %4.1f Rj %c", astro::galileanName(i), fabs(mx[i]), mx[i] >= 0 ? 'W' : 'E');
    g.drawString(b, 6, ly); ly += 12;
  }
  g.setTextDatum(textdatum_t::bottom_left);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString(String("zenith up \xB7 field tilt ") + (int)round(q / D2R) + "\xF7 for your sky", 4, cy0 + ch - 2);
}

// Telescopic Saturn: disk + ring ellipse. The ring opening B sets the ellipse's
// minor axis; the parallactic angle rotates the whole ring to the observer's sky.
void PageSolarSystem::drawSaturn(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), ch = app.contentH(), cy0 = app.contentY();
  const double D2R = 3.14159265358979323846 / 180.0;
  double jd = _time.julianDate();
  double lstR = astro::lstRad(jd, _loc.active().lon);
  double q = parallacticDeg(_st[6], _loc.active().lat, lstR) * D2R;   // sky tilt
  double B = astro::saturnRingTiltDeg(jd);

  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.drawString("Saturn - rings  [tap mid: sky]", 4, cy0 + 1);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString(String(_st[6].above ? "up" : "below horizon") + "   el " + (int)round(_st[6].elDeg)
               + "\xF7  az " + (int)round(_st[6].azDeg) + "\xF7", 4, cy0 + 16);
  g.drawString(String("rings ") + (int)round(fabs(B)) + "\xF7 open  (" + (B >= 0 ? "north" : "south") + " face)", 4, cy0 + 28);

  int cx = cw / 2, cyc = cy0 + ch / 2;
  double rMaj = 70, rMin = rMaj * fabs(sin(B * D2R));
  if (rMin < 2) rMin = 2;
  double cq = cos(q), sq = sin(q);
  // Parametric ring ellipse rotated by the parallactic angle (LGFX drawEllipse has
  // no rotation): major axis = ring plane (E-W), minor = opening. Draw outer + inner
  // (Cassini hint) edges as point-to-point segments.
  auto ringEllipse = [&](double a, double b, Color c) {
    int px = 0, py = 0;
    for (int t = 0; t <= 360; t += 12) {
      double ex = a * cos(t * D2R), ey = b * sin(t * D2R);       // ellipse, math coords
      int sx = cx + (int)round(ex * cq - ey * sq);               // rotate, then to screen
      int sy = cyc - (int)round(ex * sq + ey * cq);
      if (t > 0) g.drawLine(px, py, sx, sy, c);
      px = sx; py = sy;
    }
  };
  ringEllipse(rMaj, rMin, gTheme.fg);                            // outer ring edge
  g.fillCircle(cx, cyc, 9, gTheme.warn);                         // planet disk
  ringEllipse(rMaj, rMin, gTheme.fg);                            // redraw front arc over disk
  if (rMin >= 5) ringEllipse(rMaj * 0.62, rMin * 0.62, gTheme.dim);  // Cassini division hint
  g.setTextDatum(textdatum_t::bottom_left);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString(String("zenith up \xB7 field tilt ") + (int)round(q / D2R) + "\xF7 for your sky", 4, cy0 + ch - 2);
}
