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
  g.setTextSize(2); g.drawString("Mars", x, y); y += 20;
  g.setTextSize(1);
  char b[48];
  snprintf(b, sizeof(b), "%.2f AU  -  %.1f light-min away", distAu, ltMin);
  g.setTextColor(gTheme.fg, gTheme.bg); g.drawString(b, x, y); y += 13;

  if (_loc.active().valid) {
    astro::PlanetState ms = astro::planetState(astro::Planet::Mars, jd, _loc.active().lat, _loc.active().lon);
    if (ms.above) snprintf(b, sizeof(b), "in your sky now: el %d\xF7 az %d\xF7", (int)round(ms.elDeg), (int)round(ms.azDeg));
    else          snprintf(b, sizeof(b), "below your horizon now");
    g.setTextColor(ms.above ? gTheme.ok : gTheme.dim, gTheme.bg); g.drawString(b, x, y); y += 16;
  } else y += 4;

  // --- Rovers ---
  auto solNow = [&](time_t landing) { return now > landing ? (long)((now - landing) / SOL) : -1L; };
  struct Rover { const char* name; const char* site; const char* landed; time_t landing; const RoverInfo& info; };
  Rover rv[] = {
    { "Perseverance", "Jezero Crater",   "landed 2021-02-18", PERSEV_LANDING, _mars.perseverance() },
    { "Curiosity",    "Gale Crater",     "landed 2012-08-06", CURIO_LANDING,  _mars.curiosity() },
  };
  for (auto& r : rv) {
    long sol = solNow(r.landing);
    g.setTextColor(gTheme.accent, gTheme.bg);
    g.setTextSize(2); g.drawString(r.name, x, y); y += 19;
    g.setTextSize(1);
    g.setTextColor(gTheme.fg, gTheme.bg);
    snprintf(b, sizeof(b), "sol %ld  -  %s", sol, r.site);
    g.drawString(b, x, y); y += 12;
    if (r.info.maxSol >= 0) {                          // live NASA status
      const char* st = r.info.status.equalsIgnoreCase("active") ? "ACTIVE" : "complete";
      Color sc = r.info.status.equalsIgnoreCase("active") ? gTheme.ok : gTheme.dim;
      g.setTextColor(sc, gTheme.bg);
      snprintf(b, sizeof(b), "%s  -  last data sol %ld (%s)", st, r.info.maxSol, r.info.maxDate.c_str());
      g.drawString(b, x, y); y += 12;
      g.setTextColor(gTheme.dim, gTheme.bg);
      snprintf(b, sizeof(b), "%ld photos  -  %s", r.info.totalPhotos, r.landed);
      g.drawString(b, x, y); y += 14;
    } else {
      g.setTextColor(gTheme.dim, gTheme.bg);
      g.drawString(String(r.landed) + (_mars.status() == ProviderStatus::Error ? "  (NASA feed down)" : ""), x, y);
      y += 14;
    }
  }
}
