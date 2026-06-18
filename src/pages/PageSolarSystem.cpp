#include "PageSolarSystem.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../services/TimeService.h"
#include "../services/LocationService.h"
#include "../services/Settings.h"
#include "../providers/MarsProvider.h"
#include "../astro/Moons.h"
#include "../astro/Time.h"
#include "../assets/StarCatalog.h"
#include "../assets/MeteorShowers.h"
#include <math.h>
#include <string.h>
#include <time.h>

using astro::Planet;

static const char* kAbbrev[9] = { "Su", "Mo", "Me", "Ve", "Ma", "Ju", "Sa", "Ur", "Ne" };

static constexpr double D2R = 3.14159265358979323846 / 180.0;
static constexpr double SOL = 88775.244;             // Mars solar day (s)
static constexpr time_t PERSEV_LANDING = 1613681700; // 2021-02-18 20:55 UTC
static constexpr time_t CURIO_LANDING  = 1344230220; // 2012-08-06 05:17 UTC

// --- Sub-Earth point ("which way is Earth right now") ---------------------------
struct SubPt { double lat, lonE; };

// Given the unit vector from a rotating body to Earth in J2000 equatorial coords and
// the body's IAU pole (a0,d0) + prime-meridian angle W (deg), return the body-fixed
// sub-Earth latitude and EAST longitude (the point where Earth is at the zenith).
static SubPt subEarthFromVec(double ux, double uy, double uz,
                             double a0, double d0, double W) {
  a0 *= D2R; d0 *= D2R;
  double px = cos(d0) * cos(a0), py = cos(d0) * sin(a0), pz = sin(d0);          // pole (z)
  double nx = -sin(a0), ny = cos(a0), nz = 0;                                   // node (x)
  double mx = py * nz - pz * ny, my = pz * nx - px * nz, mz = px * ny - py * nx;// p x n (y)
  double lat = asin(ux * px + uy * py + uz * pz) / D2R;
  double A = atan2(ux * mx + uy * my + uz * mz, ux * nx + uy * ny + uz * nz) / D2R;
  double lon = fmod(A - W, 360.0); if (lon < 0) lon += 360.0;
  return { lat, lon };
}

// Mars sub-Earth point from the heliocentric Earth/Mars positions + IAU Mars frame.
static SubPt subEarthMars(double jd) {
  astro::HelioPos e = astro::heliocentricBody(2, jd);   // Earth (ecliptic lon, r)
  astro::HelioPos m = astro::heliocentricBody(3, jd);   // Mars
  double dx = e.rAu * cos(e.lonDeg * D2R) - m.rAu * cos(m.lonDeg * D2R);  // Mars->Earth
  double dy = e.rAu * sin(e.lonDeg * D2R) - m.rAu * sin(m.lonDeg * D2R);
  double eps = 23.4393 * D2R;                            // ecliptic -> equatorial
  double ux = dx, uy = dy * cos(eps), uz = dy * sin(eps);
  double r = sqrt(ux * ux + uy * uy + uz * uz); ux /= r; uy /= r; uz /= r;
  double d = jd - 2451545.0;
  return subEarthFromVec(ux, uy, uz, 317.68143, 52.88650, 176.630 + 350.89198226 * d);
}

// Moon sub-Earth point (optical libration) from the true geocentric Moon direction
// + IAU mean lunar frame. Returns east longitude in [-180,180].
static SubPt subEarthMoon(double jd, double lat, double lon) {
  astro::PlanetState s = astro::planetState(astro::Planet::Moon, jd, lat, lon);
  double ra = s.raDeg * D2R, de = s.decDeg * D2R;        // Earth->Moon direction
  double ux = -cos(de) * cos(ra), uy = -cos(de) * sin(ra), uz = -sin(de);  // Moon->Earth
  double d = jd - 2451545.0;
  SubPt p = subEarthFromVec(ux, uy, uz, 269.9949, 66.5392, 38.3213 + 13.17635815 * d);
  if (p.lonE > 180) p.lonE -= 360;
  return p;
}

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

// Angular separation (deg) between two bodies from their geocentric RA/Dec.
static double sepDeg(const astro::PlanetState& a, const astro::PlanetState& b) {
  const double D2R = 3.14159265358979323846 / 180.0;
  double r1 = a.raDeg * D2R, d1 = a.decDeg * D2R, r2 = b.raDeg * D2R, d2 = b.decDeg * D2R;
  double c = sin(d1) * sin(d2) + cos(d1) * cos(d2) * cos(r1 - r2);
  if (c > 1) c = 1; else if (c < -1) c = -1;
  return acos(c) / D2R;
}

// Rough naked-eye visibility of body i right now: combines a nominal brightness
// class with above-horizon state, sky darkness (Sun altitude) and altitude. Returns
// a short word and sets col. Honest estimate, not a magnitude calculation.
static const char* nakedEye(int i, const astro::PlanetState st[], Color& col) {
  static const int ease[9] = { 0, 3, 1, 3, 2, 3, 2, 0, 0 };  // Su Mo Me Ve Ma Ju Sa Ur Ne
  col = gTheme.dim;
  if (i <= 0) return "";                                      // Sun: n/a
  if (!st[i].above)   return "below";
  if (ease[i] == 0)   return "scope";                         // Uranus/Neptune
  double sun = st[0].elDeg, el = st[i].elDeg;
  if (sun > -0.5) return ease[i] >= 3 ? "daytime" : "washed"; // Sun up
  if (sun > -6.0) { col = gTheme.warn; return ease[i] >= 2 ? "twilight" : "low"; }
  if (el < 10)    { col = gTheme.warn; return "low"; }        // dark but near horizon
  if (ease[i] >= 3) { col = gTheme.ok; return "easy"; }
  col = gTheme.accent; return "good";
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
  computeRST(_sel);
}

// Rise / transit (culmination) / set of body idx over the next 24h, by scanning
// its altitude in 30-min steps (rise/set interpolated within the step).
void PageSolarSystem::computeRST(int idx) {
  _rst = RST{}; _rstFor = idx;
  if (idx < 0 || idx >= kN || !_time.synced() || !_loc.active().valid) return;
  double lat = _loc.active().lat, lon = _loc.active().lon;
  time_t now = time(nullptr);
  const int N = 48;                                  // 30-min steps over 24h
  float prevAlt = 0, maxAlt = -100; time_t prevT = 0, maxT = 0;
  for (int k = 0; k <= N; ++k) {
    time_t t = now + (time_t)k * 1800;
    float alt = (float)astro::planetState((Planet)idx, astro::julianDate(t), lat, lon).elDeg;
    if (alt > maxAlt) { maxAlt = alt; maxT = t; }
    if (k > 0) {
      if (prevAlt < 0 && alt >= 0 && !_rst.hasRise) { float f = prevAlt / (prevAlt - alt); _rst.rise = prevT + (time_t)(f * 1800); _rst.hasRise = true; }
      if (prevAlt >= 0 && alt < 0 && !_rst.hasSet)  { float f = prevAlt / (prevAlt - alt); _rst.set  = prevT + (time_t)(f * 1800); _rst.hasSet  = true; }
    }
    prevAlt = alt; prevT = t;
  }
  if (maxAlt > 0) { _rst.transit = maxT; _rst.transitAlt = maxAlt; _rst.hasTransit = true; }
}

void PageSolarSystem::onTouch(App& app, int x, int y) {
  int third = app.contentW() / 3;
  // Centre tap cycles sky-dome -> orbits -> Moon -> Mars -> Jupiter -> Saturn ->
  // Deep Space -> meteors.
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
  } else if (_view == 2) {                  // Moon: side tap flips near <-> far side
    _moonFar = !_moonFar;
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
    if (++_tourN >= cnt) { _tourN = 0; _view = 2; }               // orbits done -> Moon
  } else if (_view < 7) {                   // Moon->Mars->Jupiter->Saturn->Deep Space->Showers
    _view++;
  } else {                                  // Showers: one dwell -> full cycle
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

  if (_view == 1) { drawOrbit(app);     return; }
  if (_view == 2) { drawMoon(app);      return; }
  if (_view == 3) { drawMars(app);      return; }
  if (_view == 4) { drawJupiter(app);   return; }
  if (_view == 5) { drawSaturn(app);    return; }
  if (_view == 6) { drawDeepSpace(app); return; }
  if (_view == 7) { drawShowers(app);   return; }

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
  for (int i = 0; i < kN && ly < cy0 + ch - 44; ++i) {
    if (!visible(i)) continue;
    Color rc = gTheme.fg; const char* nw = nakedEye(i, _st, rc);   // naked-eye likelihood
    Color c = (i == _sel) ? gTheme.ok : rc;
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(c, gTheme.bg);
    String row = String(astro::planetName((Planet)i));
    if (i == 1) row += String("  ") + (int)astro::moonIlluminationPct(_time.julianDate()) + "% " + moonPhaseName(astro::moonPhaseDeg(_time.julianDate()));
    else if (i >= 2 && nw[0]) row += String("  ") + nw;
    g.drawString(row, 6, ly);
    g.setTextDatum(textdatum_t::top_right);
    g.setTextColor(_st[i].above ? gTheme.fg : gTheme.dim, gTheme.bg);
    char b[28];
    if (_st[i].above) snprintf(b, sizeof(b), "el %d  az %d", (int)round(_st[i].elDeg), (int)round(_st[i].azDeg));
    else              snprintf(b, sizeof(b), "below");
    g.drawString(b, cw - 6, ly);
    ly += 13;
  }

  // Closest conjunction among Moon + planets (geocentric separation). Highlighted
  // when notably close (< 5 deg). Uses the already-computed RA/Dec, so ~free.
  {
    int bi = -1, bj = -1; double best = 1e9;
    for (int i = 1; i < kN; ++i) for (int j = i + 1; j < kN; ++j) {
      double s = sepDeg(_st[i], _st[j]);
      if (s < best) { best = s; bi = i; bj = j; }
    }
    if (bi >= 0) {
      bool close = best < 5.0;
      g.setTextDatum(textdatum_t::top_left);
      g.setTextColor(close ? gTheme.warn : gTheme.dim, gTheme.bg);
      char b[48];
      snprintf(b, sizeof(b), "%s%s-%s %.1f%c apart", close ? "CONJ " : "closest ",
               astro::planetName((Planet)bi), astro::planetName((Planet)bj), best, (char)247);
      g.drawString(b, 6, cy0 + ch - 42);
    }
  }

  // Selected body's rise / transit / set (just above the badge row).
  if (_rstFor == _sel && _sel >= 0) {
    auto hm = [](time_t t) { struct tm tm; localtime_r(&t, &tm); char b[8]; snprintf(b, sizeof(b), "%02d:%02d", tm.tm_hour, tm.tm_min); return String(b); };
    String r;
    if (!_rst.hasRise && !_rst.hasSet) r = _rst.hasTransit ? "always up" : "not up in 24h";
    else {
      if (_rst.hasRise)    r += "rise " + hm(_rst.rise) + " ";
      if (_rst.hasTransit) r += "tr " + hm(_rst.transit) + " " + (int)round(_rst.transitAlt) + "\xF7 ";
      if (_rst.hasSet)     r += "set " + hm(_rst.set);
    }
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(gTheme.accent, gTheme.bg);
    g.drawString(String(astro::planetName((Planet)_sel)) + ": " + r, 6, cy0 + ch - 29);
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
  g.drawString("Orbits (top-down)  [tap mid: Moon]", 4, cy0 + 1);

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
  g.drawString("Saturn - rings  [tap mid: Deep Space]", 4, cy0 + 1);
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

// Upcoming meteor showers in date order (even far out), with ZHR, days-to-peak, and
// whether the radiant is well placed from the observer's latitude. Active showers
// are highlighted; the Agenda surfaces the next one when it's a few days away.
void PageSolarSystem::drawShowers(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.setTextSize(2); g.drawString("Meteor Showers", 4, cy0 + 1); g.setTextSize(1);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.setTextDatum(textdatum_t::top_right); g.drawString("tap mid: sky", cw - 4, cy0 + 4);

  // Colour legend: the "when" column is coloured by how well the radiant is placed.
  int ly = cy0 + 21;
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.ok, gTheme.bg);     g.drawString("great", 4, ly);
  g.setTextColor(gTheme.accent, gTheme.bg); g.drawString("good", 48, ly);
  g.setTextColor(gTheme.warn, gTheme.bg);   g.drawString("low", 86, ly);
  g.setTextColor(gTheme.dim, gTheme.bg);    g.drawString("poor", 116, ly);
  g.drawString("= radiant from here", 152, ly);

  // Fixed-X columns (the device font is proportional, so space-padding won't align).
  const int cN = 4, cP = 132, cZ = 186, cW = 238;
  int hy = cy0 + 34;
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString("shower", cN, hy); g.drawString("peak", cP, hy); g.drawString("ZHR", cZ, hy); g.drawString("when", cW, hy);

  time_t now = time(nullptr);
  struct tm tmn; localtime_r(&now, &tmn);
  int doy = tmn.tm_yday + 1;
  double lat = _loc.active().valid ? _loc.active().lat : 0.0;

  int order[kShowerCount], dtp[kShowerCount];
  for (int i = 0; i < kShowerCount; ++i) {
    order[i] = i;
    dtp[i] = (meteorDOY(kShowers[i].pkM, kShowers[i].pkD) - doy + 365) % 365;
  }
  for (int i = 1; i < kShowerCount; ++i) {                 // insertion sort by days-to-peak
    int k = order[i], j = i - 1;
    while (j >= 0 && dtp[order[j]] > dtp[k]) { order[j + 1] = order[j]; --j; }
    order[j + 1] = k;
  }

  int y = cy0 + 47;
  char b[16];
  for (int oi = 0; oi < kShowerCount && y < cy0 + ch - 2; ++oi) {
    const MeteorShower& s = kShowers[order[oi]];
    int d = dtp[order[oi]];
    int st = meteorDOY(s.stM, s.stD), en = meteorDOY(s.enM, s.enD);
    bool active = (st <= en) ? (doy >= st && doy <= en) : (doy >= st || doy <= en);
    int maxAlt = 90 - (int)fabs(lat - s.radDec);
    Color vc = maxAlt > 50 ? gTheme.ok : maxAlt > 25 ? gTheme.accent : maxAlt > 5 ? gTheme.warn : gTheme.dim;
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(active ? gTheme.warn : gTheme.fg, gTheme.bg);
    g.drawString(s.name, cN, y);
    g.setTextColor(gTheme.dim, gTheme.bg);
    snprintf(b, sizeof(b), "%d/%02d", s.pkM, s.pkD); g.drawString(b, cP, y);
    snprintf(b, sizeof(b), "%d", s.zhr);             g.drawString(b, cZ, y);
    g.setTextColor(vc, gTheme.bg);
    if (active)      g.drawString("NOW", cW, y);
    else if (d == 0) g.drawString("peak", cW, y);
    else { snprintf(b, sizeof(b), "+%dd", d); g.drawString(b, cW, y); }
    y += 13;
  }
}

void PageSolarSystem::drawBodyOverlay(App& app, int mx, int my, int mw, int mh,
                                      double lonMin, double lonMax, double slat, double slon,
                                      bool boundary, uint16_t col, const char* label) {
  auto& g = app.display().gfx();
  auto PX = [&](double lon) {
    double L = lon; while (L < lonMin) L += 360; while (L >= lonMin + 360) L -= 360;
    return mx + (int)((L - lonMin) / (lonMax - lonMin) * mw);
  };
  auto PY = [&](double lat) { return my + (int)((90.0 - lat) / 180.0 * mh); };

  if (boundary) {
    double t = tan(slat * D2R);
    if (fabs(t) < 1e-3) {
      g.drawFastVLine(PX(slon - 90), my, mh, col);
      g.drawFastVLine(PX(slon + 90), my, mh, col);
    } else {
      int px = -1, py = -1;
      for (int i = 0; i <= 180; ++i) {
        double lon = lonMin + (lonMax - lonMin) * i / 180.0;
        double phib = atan(-cos((lon - slon) * D2R) / t) / D2R;
        int xx = PX(lon), yy = PY(phib);
        if (px >= 0 && abs(yy - py) < mh / 2) g.drawLine(px, py, xx, yy, col);
        px = xx; py = yy;
      }
    }
  }
  int ex = PX(slon), ey = PY(slat);
  if (ex < mx || ex > mx + mw) return;
  g.drawFastHLine(ex - 5, ey, 11, col);
  g.drawFastVLine(ex, ey - 5, 11, col);
  g.drawCircle(ex, ey, 4, col);
  bool right = ex < mx + mw - 40;
  g.setTextColor(col, gTheme.bg);
  g.setTextDatum(right ? textdatum_t::middle_left : textdatum_t::middle_right);
  g.drawString(label, right ? ex + 7 : ex - 7, ey);
}

// The Moon: live phase / illumination / distance, plus a map of real landing sites -
// crewed Apollo, robotic past, and the new commercial CLPS landers. Side tap flips
// between the near side (always faces Earth) and the far side (Chang'e country). All
// plotted missions have landed, so coords are real; upcoming flights are undated.
void PageSolarSystem::drawMoon(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY();
  g.setTextDatum(textdatum_t::top_left); g.setTextSize(1);
  double jd = _time.julianDate();
  int x = 6, y = cy0 + 4;
  bool far = _moonFar;

  double illum = astro::moonIlluminationPct(jd);
  double ph = astro::moonPhaseDeg(jd);
  const char* phName =
      ph < 22.5  ? "New"           : ph < 67.5  ? "Waxing crescent" :
      ph < 112.5 ? "First quarter" : ph < 157.5 ? "Waxing gibbous"  :
      ph < 202.5 ? "Full"          : ph < 247.5 ? "Waning gibbous"  :
      ph < 292.5 ? "Last quarter"  : ph < 337.5 ? "Waning crescent" : "New";

  g.setTextColor(gTheme.mono ? gTheme.fg : rgb565(205, 208, 215), gTheme.bg);
  g.setTextSize(2); g.drawString("Moon", x, y); g.setTextSize(1);
  g.setTextColor(gTheme.dim, gTheme.bg); g.setTextDatum(textdatum_t::top_right);
  g.drawString("[tap mid: Mars]", cw - 4, cy0 + 2); g.setTextDatum(textdatum_t::top_left);
  char b[64];
  snprintf(b, sizeof(b), "%s  %d%% lit", phName, (int)round(illum));
  g.setTextColor(gTheme.fg, gTheme.bg); g.drawString(b, x + 56, y + 3); y += 18;

  if (_loc.active().valid) {
    astro::PlanetState mo = astro::planetState(astro::Planet::Moon, jd, _loc.active().lat, _loc.active().lon);
    int km = (int)round(mo.distanceAu * 149597870.7);
    if (mo.above) snprintf(b, sizeof(b), "%d km  in your sky: el %d\xF7 az %d\xF7", km, (int)round(mo.elDeg), (int)round(mo.azDeg));
    else          snprintf(b, sizeof(b), "%d km  below your horizon now", km);
    g.setTextColor(mo.above ? gTheme.ok : gTheme.dim, gTheme.bg); g.drawString(b, x, y); y += 13;
  }

  // Map: near side spans lon -90..90 (E+, Crisium right); far side spans 90..270.
  int mx = 6, mw = cw - 12, my = y + 1, mh = 96;
  double lonMin = far ? 90.0 : -90.0, lonMax = lonMin + 180.0;
  // Surface tones honour the red dark-adapt palette (mono) instead of a fixed grey.
  Color litCol   = gTheme.mono ? rgb565(120, 22, 8) : rgb565(95, 95, 108);
  Color nightCol = gTheme.mono ? rgb565(28, 5, 0)   : rgb565(22, 22, 28);
  g.fillRect(mx, my, mw, mh, litCol);                // sunlit lunar surface
  auto MX = [&](float lonE) { double L = lonE; if (far && L < 0) L += 360; return mx + (int)((L - lonMin) / 180.0 * mw); };
  auto MY = [&](float lat)  { return my + (int)((90.0f - lat) / 180.0f * mh); };
  auto onDisc = [&](float lonE) { float a = fabsf(lonE); return far ? a >= 90.0f : a <= 90.0f; };

  // Shade the night side so the lit area matches the displayed phase (sub-Solar ~on
  // the equator at lamS; >90 deg away = night). Waxing lights the east/Crisium limb.
  double frac = illum / 100.0;
  double phAng = acos(fmax(-1.0, fmin(1.0, 2.0 * frac - 1.0))) / D2R;
  double lamS = (ph < 180.0 ? 1.0 : -1.0) * phAng;
  for (int xx = 0; xx < mw; ++xx) {
    double lon = lonMin + (double)xx / mw * 180.0;
    double dl = lon - lamS; while (dl > 180) dl -= 360; while (dl < -180) dl += 360;
    if (fabs(dl) > 90.0) g.drawFastVLine(mx + xx, my, mh, nightCol);
  }
  g.drawRect(mx, my, mw, mh, gTheme.grid);
  g.drawFastHLine(mx, my + mh / 2, mw, gTheme.dim);
  g.drawFastVLine(mx + mw / 2, my, mh, gTheme.dim);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString(far ? "Far side  [tap side: near]" : "Near side  [tap side: far]", mx + 3, my + 2);

  // maria / craters for orientation (near + far side; filtered to the shown hemisphere)
  struct Feat { const char* name; float lat, lonE; };
  static const Feat ft[] = {
    { "Imbrium", 33.0f, -16.0f }, { "Tycho", -43.3f, -11.4f },               // near
    { "Aitken", -53.0f, -169.0f }, { "Tsiolk.", -20.4f, 129.1f }, { "Moscov.", 27.3f, 147.4f }, // far
  };
  for (auto& f : ft) {
    if (!onDisc(f.lonE)) continue;
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.setTextDatum(textdatum_t::middle_center);
    g.drawString(f.name, MX(f.lonE), MY(f.lat));
  }

  // landing sites: type 0=crewed Apollo, 1=robotic past, 2=2024+ commercial/CLPS
  struct Site { const char* tag; float lat, lonE; uint8_t type; };
  static const Site st[] = {
    { "A11",  0.67f,  23.47f, 0 }, { "", -3.01f, -23.42f, 0 }, { "", -3.65f, -17.47f, 0 },
    { "",    26.13f,   3.63f, 0 }, { "", -8.97f,  15.50f, 0 }, { "A17", 20.19f, 30.77f, 0 },
    { "",     7.08f, -64.37f, 1 }, { "", -0.51f,  56.36f, 1 }, { "",  38.24f, -34.99f, 1 },
    { "",    -2.47f, -43.34f, 1 }, { "", 44.12f, -19.51f, 1 }, { "",  43.06f, -51.92f, 1 },
    { "C-3", -69.37f, 32.32f, 1 }, { "", -13.30f, 25.20f, 1 },
    { "Firefly", 18.56f, 61.81f, 2 }, { "", -80.13f, 1.44f, 2 }, { "", -84.5f, -6.0f, 2 },
    { "CE4", -45.5f, 177.6f, 1 }, { "CE6", -41.6f, -153.9f, 2 },              // far side
  };
  for (auto& s : st) {
    if (!onDisc(s.lonE)) continue;
    int sx = MX(s.lonE), sy = MY(s.lat);
    Color c = s.type == 0 ? gTheme.warn : s.type == 1 ? gTheme.ok : gTheme.accent;
    g.fillCircle(sx, sy, s.type == 0 ? 2 : 1, c);
    if (s.tag[0]) {
      g.setTextColor(c, gTheme.bg);
      bool right = sx < mx + mw - 48;
      g.setTextDatum(right ? textdatum_t::middle_left : textdatum_t::middle_right);
      g.drawString(s.tag, right ? sx + 4 : sx - 4, sy);
    }
  }
  // Sun (centre of the lit area) + sub-Earth/libration markers; off-disc ones skip.
  drawBodyOverlay(app, mx, my, mw, mh, lonMin, lonMax, 0, lamS, false, gTheme.warn, "Sun");
  if (_loc.active().valid && !far) {
    SubPt se = subEarthMoon(jd, _loc.active().lat, _loc.active().lon);
    drawBodyOverlay(app, mx, my, mw, mh, lonMin, lonMax, se.lat, se.lonE, false, gTheme.accent, "Earth");
  }
  g.setTextDatum(textdatum_t::bottom_left);
  g.setTextColor(gTheme.warn, gTheme.bg);   g.drawString("crewed", mx + 3, my + mh - 2);
  g.setTextColor(gTheme.ok, gTheme.bg);     g.drawString("robotic", mx + 46, my + mh - 2);
  g.setTextColor(gTheme.dim, gTheme.bg);    g.drawString("shaded=night", mx + 92, my + mh - 2);
  y = my + mh + 3;

  // summary: past missions are real/dated; Artemis reflects the 2026 reset.
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.drawString("Apollo 11-17: 12 humans walked here, 1969-72", x, y); y += 12;
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString("Recent: Blue Ghost & IM-2 '25, SLIM '24, C-3 '23", x, y); y += 12;
  g.drawString("Far side: Chang'e 4 '19, Chang'e 6 sample '24", x, y); y += 12;
  g.setTextColor(gTheme.ok, gTheme.bg);
  g.drawString("Artemis II: 4 crew round the Moon, Apr 2026", x, y); y += 12;
  g.setTextColor(gTheme.warn, gTheme.bg);
  g.drawString("Soon '26: Chang'e 7 S.pole . IM-3 . BlueGhost 2", x, y); y += 12;
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString("Planned: Artemis III LEO '27 . IV landing '28", x, y);
}

void PageSolarSystem::drawMars(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY();
  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(1);
  double jd = _time.julianDate();
  time_t now = time(nullptr);
  int x = 6, y = cy0 + 4;

  astro::HelioPos e = astro::heliocentricBody(2, jd);   // Earth
  astro::HelioPos m = astro::heliocentricBody(3, jd);   // Mars
  double dl = (m.lonDeg - e.lonDeg) * D2R;
  double distAu = sqrt(e.rAu * e.rAu + m.rAu * m.rAu - 2 * e.rAu * m.rAu * cos(dl));
  double ltMin = distAu * 149597870.7 / 299792.458 / 60.0;

  g.setTextColor(gTheme.warn, gTheme.bg);
  g.setTextSize(2); g.drawString("Mars", x, y);
  g.setTextSize(1);
  g.setTextColor(gTheme.dim, gTheme.bg); g.setTextDatum(textdatum_t::top_right);
  g.drawString("[tap mid: Jupiter]", cw - 4, cy0 + 2); g.setTextDatum(textdatum_t::top_left);
  char b[56];
  snprintf(b, sizeof(b), "%.2f AU  %.1f lt-min", distAu, ltMin);
  g.setTextColor(gTheme.fg, gTheme.bg); g.drawString(b, x + 56, y + 3); y += 18;
  if (_loc.active().valid) {
    astro::PlanetState ms = astro::planetState(astro::Planet::Mars, jd, _loc.active().lat, _loc.active().lon);
    if (ms.above) snprintf(b, sizeof(b), "in your sky now: el %d\xF7 az %d\xF7", (int)round(ms.elDeg), (int)round(ms.azDeg));
    else          snprintf(b, sizeof(b), "below your horizon now");
    g.setTextColor(ms.above ? gTheme.ok : gTheme.dim, gTheme.bg); g.drawString(b, x, y); y += 13;
  }

  struct Rover { const char* name; const char* lbl; const char* site; const char* landed; time_t landing; float lat, lonE; const RoverInfo& info; };
  Rover rv[] = {
    { "Perseverance", "Pe", "Jezero",     "2021-02-18", PERSEV_LANDING, 18.44f,  77.45f, _mars.perseverance() },
    { "Curiosity",    "Cu", "Gale",       "2012-08-06", CURIO_LANDING,  -4.59f, 137.44f, _mars.curiosity() },
  };
  int mx = 6, mw = cw - 12, my = y + 1, mh = 96;
  Color ochre = gTheme.mono ? rgb565(85, 16, 4)  : rgb565(70, 34, 22);   // red-adapt aware
  Color ice   = gTheme.mono ? rgb565(255, 90, 60) : rgb565(225, 230, 235);
  g.fillRect(mx, my, mw, mh, ochre);                 // dim Mars surface
  auto FX = [&](float lonE) { return mx + (int)(lonE / 360.0f * mw); };
  auto FY = [&](float lat)  { return my + (int)((90.0f - lat) / 180.0f * mh); };
  int capH = FY(70.0f) - my;
  g.fillRect(mx, my, mw, capH, ice);                 // N polar cap
  g.fillRect(mx, my + mh - capH, mw, capH, ice);     // S polar cap
  g.drawRect(mx, my, mw, mh, gTheme.grid);
  g.drawFastHLine(mx, my + mh / 2, mw, gTheme.dim);
  for (int lon = 90; lon < 360; lon += 90) g.drawFastVLine(mx + lon * mw / 360, my, mh, gTheme.dim);
  g.setTextColor(gTheme.dim, gTheme.bg); g.setTextDatum(textdatum_t::top_left);
  g.drawString("Mars surface", mx + 3, my + 2);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.setTextDatum(textdatum_t::top_right);  g.drawString("ice", mx + mw - 3, my + 1);
  g.setTextDatum(textdatum_t::bottom_right); g.drawString("ice", mx + mw - 3, my + mh - 1);

  struct Feat { const char* name; float lat, lonE; uint8_t kind; };
  static const Feat ft[] = {
    { "Olympus",   18.65f, 226.2f, 0 }, { "Tharsis", 0.8f, 247.0f, 0 }, { "Elysium", 25.0f, 147.0f, 0 },
    { "Marineris", -13.9f, 300.0f, 2 }, { "Hellas", -42.4f, 70.5f, 1 },
    { "Argyre",   -49.7f,  316.0f, 1 }, { "Utopia", 46.7f, 117.5f, 1 },
  };
  for (auto& f : ft) {
    int fx = FX(f.lonE), fy = FY(f.lat);
    if (f.kind == 0)      g.fillTriangle(fx, fy - 3, fx - 3, fy + 2, fx + 3, fy + 2, gTheme.accent);
    else if (f.kind == 1) g.drawCircle(fx, fy, 3, gTheme.accent);
    else                  g.drawFastHLine(fx - 4, fy, 9, gTheme.accent);
    bool right = fx < mx + mw - 54;
    g.setTextColor(gTheme.accent, gTheme.bg);
    g.setTextDatum(right ? textdatum_t::middle_left : textdatum_t::middle_right);
    g.drawString(f.name, right ? fx + 5 : fx - 5, fy);
  }
  for (auto& r : rv) {
    int rx = FX(r.lonE), ry = FY(r.lat);
    g.fillCircle(rx, ry, 2, gTheme.ok);
    g.setTextColor(gTheme.fg, gTheme.bg); g.setTextDatum(textdatum_t::middle_left);
    g.drawString(r.lbl, rx + 4, ry);
  }
  SubPt se = subEarthMars(jd);
  drawBodyOverlay(app, mx, my, mw, mh, 0, 360, se.lat, se.lonE, true, gTheme.accent, "Earth");
  y = my + mh + 3;
  g.setTextDatum(textdatum_t::top_left); g.setTextColor(gTheme.accent, gTheme.bg);
  snprintf(b, sizeof(b), "Earth over %d\xF7E %d\xF7%c now - circled side faces us", (int)round(se.lonE),
           (int)round(fabs(se.lat)), se.lat >= 0 ? 'N' : 'S');
  g.drawString(b, x, y); y += 13;

  auto solNow = [&](time_t landing) { return now > landing ? (long)((now - landing) / SOL) : -1L; };
  g.setTextDatum(textdatum_t::top_left);
  for (auto& r : rv) {
    long sol = solNow(r.landing);
    g.setTextColor(gTheme.accent, gTheme.bg); g.drawString(r.name, x, y);
    if (r.info.maxSol >= 0) {
      bool act = r.info.status.equalsIgnoreCase("active");
      g.setTextColor(act ? gTheme.ok : gTheme.dim, gTheme.bg);
      snprintf(b, sizeof(b), "sol %ld %s  last %s", sol, act ? "ACTIVE" : "done", r.info.maxDate.c_str());
    } else {
      g.setTextColor(gTheme.dim, gTheme.bg);
      snprintf(b, sizeof(b), "sol %ld  %s  %s", sol, r.site, _mars.status() == ProviderStatus::Error ? "(no feed)" : "");
    }
    g.drawString(b, x + 78, y); y += 13;
  }
}

// Iconic active deep-space missions. Receding-probe distance is extrapolated from a
// reference epoch + recession rate; cruising flagships show live age + next milestone.
void PageSolarSystem::drawDeepSpace(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY();
  float yr = 2000.0f + (float)((_time.julianDate() - 2451545.0) / 365.25);

  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.setTextSize(2); g.drawString("Deep Space", 6, cy0 + 2); g.setTextSize(1);
  g.setTextColor(gTheme.dim, gTheme.bg); g.setTextDatum(textdatum_t::top_right);
  g.drawString("[tap mid: meteors]", cw - 4, cy0 + 9); g.setTextDatum(textdatum_t::top_left);
  int y = cy0 + 22;
  char b[64];

  struct DS { const char* name; const char* note; float au, rate, baseYr; };
  static const DS d[] = {
    {"Voyager 1",   "interstellar", 165.6f, 3.58f, 2025.5f},
    {"Voyager 2",   "interstellar", 138.2f, 3.27f, 2025.5f},
    {"New Horizons","Kuiper Belt",   60.5f, 2.95f, 2025.5f},
  };
  for (auto& m : d) {
    float dist = m.au + m.rate * (yr - m.baseYr);
    g.setTextColor(gTheme.fg, gTheme.bg);  g.drawString(m.name, 6, y);
    snprintf(b, sizeof(b), "%d AU  %.0f lt-hr  %s", (int)dist, dist * 8.317f / 60.0f, m.note);
    g.setTextColor(gTheme.dim, gTheme.bg); g.drawString(b, 92, y);
    y += 13;
  }
  y += 4;

  struct Cruise { const char* name; const char* target; float launchYr, mileYr; const char* mlabel, *arrival; };
  static const Cruise cr[] = {
    {"Psyche",      "to metal asteroid 16 Psyche", 2023.78f, 2026.37f, "Mars flyby",  "16 Psyche Aug'29"},
    {"Eur.Clipper", "to Jupiter ocean moon Europa", 2024.79f, 2026.92f, "Earth flyby", "Jupiter Apr'30"},
  };
  for (auto& c : cr) {
    g.setTextColor(gTheme.fg, gTheme.bg);  g.drawString(c.name, 6, y);
    g.setTextColor(gTheme.dim, gTheme.bg); g.drawString(c.target, 92, y); y += 11;
    char mile[40]; float dt = c.mileYr - yr;
    if (dt <= 0)       snprintf(mile, sizeof(mile), "%s done", c.mlabel);
    else if (dt < 1)   snprintf(mile, sizeof(mile), "%s in %dmo", c.mlabel, (int)roundf(dt * 12));
    else               snprintf(mile, sizeof(mile), "%s in %.1fy", c.mlabel, dt);
    snprintf(b, sizeof(b), "%.1fy flying . %s . %s", yr - c.launchYr, mile, c.arrival);
    g.setTextColor(gTheme.accent, gTheme.bg); g.drawString(b, 14, y); y += 15;
  }

  g.setTextColor(gTheme.fg, gTheme.bg);  g.drawString("JWST", 6, y);
  g.setTextColor(gTheme.dim, gTheme.bg); g.drawString("1.5M km out at L2 . sees 1st galaxies", 92, y); y += 13;
  g.setTextColor(gTheme.fg, gTheme.bg);  g.drawString("Parker SP", 6, y);
  g.setTextColor(gTheme.dim, gTheme.bg); g.drawString("~690,000 km/h - fastest ever made", 92, y);
}
