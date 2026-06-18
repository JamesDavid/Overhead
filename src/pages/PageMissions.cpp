#include "PageMissions.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../services/TimeService.h"
#include "../services/LocationService.h"
#include "../providers/MarsProvider.h"
#include "../astro/SolarSystem.h"
#include <math.h>
#include <time.h>

static constexpr double D2R = 3.14159265358979323846 / 180.0;
static constexpr double SOL = 88775.244;            // Mars solar day (s)

// Landing epochs (UTC) for the active rovers.
static constexpr time_t PERSEV_LANDING = 1613681700; // 2021-02-18 20:55 UTC
static constexpr time_t CURIO_LANDING  = 1344230220; // 2012-08-06 05:17 UTC

void PageMissions::tick(App& app, uint32_t nowMs) {
  if (!_dirty && nowMs - _lastDraw < 30000) return;  // slow-changing
  _dirty = false; _lastDraw = nowMs;
  draw(app);
}

void PageMissions::onTouch(App& app, int x, int y) {
  int third = app.contentW() / 3;
  if (x >= third && x <= 2 * third) { _view = (_view + 1) % 3; _dirty = true; }  // centre cycles
}

void PageMissions::draw(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  g.fillRect(0, cy0, cw, ch, gTheme.bg);
  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(1);
  if (!_time.synced()) {
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString("waiting for time sync...", cw / 2, cy0 + ch / 2);
    return;
  }
  if (_view == 0) drawMars(app); else if (_view == 1) drawMoon(app); else drawDeepSpace(app);
}

void PageMissions::drawMars(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(1);
  double jd = _time.julianDate();
  time_t now = time(nullptr);
  int x = 6, y = cy0 + 4;

  // --- Mars, live ---
  astro::HelioPos e = astro::heliocentricBody(2, jd);   // Earth
  astro::HelioPos m = astro::heliocentricBody(3, jd);   // Mars
  double dl = (m.lonDeg - e.lonDeg) * D2R;
  double distAu = sqrt(e.rAu * e.rAu + m.rAu * m.rAu - 2 * e.rAu * m.rAu * cos(dl));
  double ltMin = distAu * 149597870.7 / 299792.458 / 60.0;

  g.setTextColor(gTheme.warn, gTheme.bg);
  g.setTextSize(2); g.drawString("Mars", x, y);
  g.setTextSize(1);
  g.setTextColor(gTheme.dim, gTheme.bg); g.setTextDatum(textdatum_t::top_right);
  g.drawString("[tap mid: Moon]", cw - 4, cy0 + 2); g.setTextDatum(textdatum_t::top_left);
  char b[56];
  snprintf(b, sizeof(b), "%.2f AU  %.1f lt-min", distAu, ltMin);
  g.setTextColor(gTheme.fg, gTheme.bg); g.drawString(b, x + 56, y + 3); y += 18;
  if (_loc.active().valid) {
    astro::PlanetState ms = astro::planetState(astro::Planet::Mars, jd, _loc.active().lat, _loc.active().lon);
    if (ms.above) snprintf(b, sizeof(b), "in your sky now: el %d\xF7 az %d\xF7", (int)round(ms.elDeg), (int)round(ms.azDeg));
    else          snprintf(b, sizeof(b), "below your horizon now");
    g.setTextColor(ms.above ? gTheme.ok : gTheme.dim, gTheme.bg); g.drawString(b, x, y); y += 13;
  }

  // --- Mars surface map with the rover landing sites ---
  struct Rover { const char* name; const char* lbl; const char* site; const char* landed; time_t landing; float lat, lonE; const RoverInfo& info; };
  Rover rv[] = {
    { "Perseverance", "Pe", "Jezero",     "2021-02-18", PERSEV_LANDING, 18.44f,  77.45f, _mars.perseverance() },
    { "Curiosity",    "Cu", "Gale",       "2012-08-06", CURIO_LANDING,  -4.59f, 137.44f, _mars.curiosity() },
  };
  int mx = 6, mw = cw - 12, my = y + 1, mh = 96;
  g.fillRect(mx, my, mw, mh, rgb565(70, 34, 22));    // dim Mars ochre
  auto FX = [&](float lonE) { return mx + (int)(lonE / 360.0f * mw); };
  auto FY = [&](float lat)  { return my + (int)((90.0f - lat) / 180.0f * mh); };
  int capH = FY(70.0f) - my;                          // ice caps poleward of ~70 deg
  g.fillRect(mx, my, mw, capH, rgb565(225, 230, 235));            // N polar cap
  g.fillRect(mx, my + mh - capH, mw, capH, rgb565(225, 230, 235));// S polar cap
  g.drawRect(mx, my, mw, mh, gTheme.grid);
  g.drawFastHLine(mx, my + mh / 2, mw, gTheme.dim);  // equator
  for (int lon = 90; lon < 360; lon += 90) g.drawFastVLine(mx + lon * mw / 360, my, mh, gTheme.dim);
  g.setTextColor(gTheme.dim, gTheme.bg); g.setTextDatum(textdatum_t::top_left);
  g.drawString("Mars surface", mx + 3, my + 2);
  g.setTextColor(rgb565(120, 130, 140), gTheme.bg);
  g.setTextDatum(textdatum_t::top_right);  g.drawString("ice", mx + mw - 3, my + 1);
  g.setTextDatum(textdatum_t::bottom_right); g.drawString("ice", mx + mw - 3, my + mh - 1);

  // Major surface features (real areographic coords, planetocentric east lon).
  // kind: 0=volcano/peak (triangle), 1=basin/lowland (ring), 2=canyon (bar).
  struct Feat { const char* name; float lat, lonE; uint8_t kind; };
  static const Feat ft[] = {
    { "Olympus",   18.65f, 226.2f, 0 },   // tallest volcano in the solar system
    { "Tharsis",    0.8f,  247.0f, 0 },   // the three aligned Tharsis Montes
    { "Elysium",   25.0f,  147.0f, 0 },
    { "Marineris", -13.9f, 300.0f, 2 },   // Valles Marineris canyon system
    { "Hellas",   -42.4f,   70.5f, 1 },   // giant impact basin
    { "Argyre",   -49.7f,  316.0f, 1 },   // southern impact basin
    { "Utopia",    46.7f,  117.5f, 1 },   // largest basin; Viking 2 / Zhurong
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
  for (auto& r : rv) {                                 // rover sites on top
    int rx = FX(r.lonE), ry = FY(r.lat);
    g.fillCircle(rx, ry, 2, gTheme.ok);
    g.setTextColor(gTheme.fg, gTheme.bg); g.setTextDatum(textdatum_t::middle_left);
    g.drawString(r.lbl, rx + 4, ry);
  }
  y = my + mh + 3;

  // --- Rover status (compact) ---
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

// The Moon: live phase / illumination / distance, plus a near-side map of real
// landing sites - crewed Apollo, robotic past, and the new commercial CLPS landers -
// with a few maria/craters for orientation. Everything plotted is a mission that has
// already landed, so all coordinates are real; upcoming flights are named but left
// undated because lunar schedules slip constantly (no fabricated dates).
void PageMissions::drawMoon(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  g.setTextDatum(textdatum_t::top_left); g.setTextSize(1);
  double jd = _time.julianDate();
  int x = 6, y = cy0 + 4;

  // --- live Moon ---
  double illum = astro::moonIlluminationPct(jd);
  double ph = astro::moonPhaseDeg(jd);
  const char* phName =
      ph < 22.5  ? "New"           : ph < 67.5  ? "Waxing crescent" :
      ph < 112.5 ? "First quarter" : ph < 157.5 ? "Waxing gibbous"  :
      ph < 202.5 ? "Full"          : ph < 247.5 ? "Waning gibbous"  :
      ph < 292.5 ? "Last quarter"  : ph < 337.5 ? "Waning crescent" : "New";

  g.setTextColor(rgb565(205, 208, 215), gTheme.bg);
  g.setTextSize(2); g.drawString("Moon", x, y); g.setTextSize(1);
  g.setTextColor(gTheme.dim, gTheme.bg); g.setTextDatum(textdatum_t::top_right);
  g.drawString("[tap mid: Deep Spc]", cw - 4, cy0 + 2); g.setTextDatum(textdatum_t::top_left);
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

  // --- near-side map: lon -90..+90 (E+, Crisium on the right), lat -90..90 ---
  int mx = 6, mw = cw - 12, my = y + 1, mh = 96;
  g.fillRect(mx, my, mw, mh, rgb565(48, 48, 54));    // lunar grey
  g.drawRect(mx, my, mw, mh, gTheme.grid);
  g.drawFastHLine(mx, my + mh / 2, mw, gTheme.dim);  // equator
  g.drawFastVLine(mx + mw / 2, my, mh, gTheme.dim);  // prime meridian (disc centre)
  auto MX = [&](float lonE) { return mx + (int)((lonE + 90.0f) / 180.0f * mw); };
  auto MY = [&](float lat)  { return my + (int)((90.0f - lat) / 180.0f * mh); };
  g.setTextColor(gTheme.dim, gTheme.bg); g.drawString("Near side", mx + 3, my + 2);

  // maria / craters for orientation (real selenographic coords). Kept to features
  // that sit in open areas - the landing-site tags already mark Tranquillitatis
  // (Apollo 11) and Crisium (Firefly), so labelling those too would just collide.
  struct Feat { const char* name; float lat, lonE; };
  static const Feat ft[] = {
    { "Imbrium", 33.0f, -16.0f }, { "Tycho", -43.3f, -11.4f },
  };
  for (auto& f : ft) {
    g.setTextColor(rgb565(120, 122, 130), gTheme.bg);
    g.setTextDatum(textdatum_t::middle_center);
    g.drawString(f.name, MX(f.lonE), MY(f.lat));
  }

  // landing sites: type 0=crewed Apollo, 1=robotic past, 2=2024+ commercial CLPS
  struct Site { const char* tag; float lat, lonE; uint8_t type; };
  static const Site st[] = {
    { "A11",  0.67f,  23.47f, 0 }, { "", -3.01f, -23.42f, 0 }, { "", -3.65f, -17.47f, 0 },
    { "",    26.13f,   3.63f, 0 }, { "", -8.97f,  15.50f, 0 }, { "A17", 20.19f, 30.77f, 0 },
    { "",     7.08f, -64.37f, 1 }, { "", -0.51f,  56.36f, 1 }, { "",  38.24f, -34.99f, 1 },
    { "",    -2.47f, -43.34f, 1 }, { "", 44.12f, -19.51f, 1 }, { "",  43.06f, -51.92f, 1 },
    { "C-3", -69.37f, 32.32f, 1 }, { "", -13.30f, 25.20f, 1 },
    { "Firefly", 18.56f, 61.81f, 2 }, { "", -80.13f, 1.44f, 2 }, { "", -84.5f, -6.0f, 2 },
  };
  for (auto& s : st) {
    if (s.lonE < -90 || s.lonE > 90) continue;       // far side: not on this disc
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
  g.setTextDatum(textdatum_t::bottom_left);
  g.setTextColor(gTheme.warn, gTheme.bg);   g.drawString("crewed", mx + 3, my + mh - 2);
  g.setTextColor(gTheme.ok, gTheme.bg);     g.drawString("robotic", mx + 50, my + mh - 2);
  g.setTextColor(gTheme.accent, gTheme.bg); g.drawString("2024+ CLPS", mx + 102, my + mh - 2);
  y = my + mh + 3;

  // --- summary (past dates are real; the future line is undated on purpose) ---
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.drawString("Apollo 11-17: 12 humans walked here, 1969-72", x, y); y += 12;
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString("Recent: Blue Ghost & IM-2 '25, SLIM '24, C-3 '23", x, y); y += 12;
  g.drawString("Far side: Chang'e 4 '19, Chang'e 6 sample '24", x, y); y += 12;
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString("Next: Artemis II crew flyby + CLPS (planned)", x, y);
}

// Iconic active deep-space missions. Distance for the receding probes is
// extrapolated from a reference epoch + recession rate (good to ~1 AU); en-route /
// stationed missions show their target instead. Light-time turns "far" into "wow".
void PageMissions::drawDeepSpace(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  float yr = 2000.0f + (float)((_time.julianDate() - 2451545.0) / 365.25);

  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.setTextSize(2); g.drawString("Deep Space", 6, cy0 + 2); g.setTextSize(1);
  g.setTextColor(gTheme.dim, gTheme.bg); g.drawString("[tap mid: Mars]", 150, cy0 + 9);
  int y = cy0 + 24;

  struct DS { const char* name; const char* note; float au; float rate; float baseYr; };
  static const DS d[] = {
    {"Voyager 1",   "interstellar",         165.6f, 3.58f, 2025.5f},
    {"Voyager 2",   "interstellar",         138.2f, 3.27f, 2025.5f},
    {"New Horizons","Kuiper Belt",           60.5f, 2.95f, 2025.5f},
    {"Psyche",      "metal asteroid '29",     0,    0,     0},
    {"Eur.Clipper", "Jupiter/Europa '30",     0,    0,     0},
    {"JWST",        "Sun-Earth L2 1.5Mkm",    0,    0,     0},
    {"Parker SP",   "skims the Sun",          0,    0,     0},
  };
  char b[56];
  for (auto& m : d) {
    if (y > cy0 + ch - 12) break;
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(gTheme.fg, gTheme.bg);  g.drawString(m.name, 6, y);
    if (m.au > 0) {
      float dist = m.au + m.rate * (yr - m.baseYr);
      snprintf(b, sizeof(b), "%d AU  %.0f lt-hr  %s", (int)dist, dist * 8.317f / 60.0f, m.note);
    } else {
      snprintf(b, sizeof(b), "-> %s", m.note);
    }
    g.setTextColor(gTheme.dim, gTheme.bg);  g.drawString(b, 92, y);
    y += 13;
  }
}
