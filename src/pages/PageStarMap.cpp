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

void PageStarMap::onTouch(App& app, int x, int y) {
  if (x <= 80 && y >= app.contentH() - 20) {       // bottom-left badge: mag limit
    _magLimit = (_magLimit >= 4.0f) ? 2.0f : _magLimit + 1.0f;
    _dirty = true; return;
  }
  if (x >= app.contentW() - 46 && y >= app.contentH() - 20) {   // bottom-right: SS overlay
    _showSS = !_showSS; _dirty = true; return;
  }
  int third = app.contentW() / 3;
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

void PageStarMap::tick(App& app, uint32_t nowMs) {
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

  // Horizon circle + cardinal marks.
  g.drawCircle(cx, cy, R, gTheme.grid);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.setTextDatum(textdatum_t::middle_center);
  g.drawString("N", cx, cy - R - 5); g.drawString("S", cx, cy + R + 5);
  g.drawString("E", cx + R + 5, cy); g.drawString("W", cx - R - 5, cy);

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
        project(*b, jd, latRad, lst, cx, cy, R, bx, by, bl))
      g.drawLine(ax, ay, bx, by, gTheme.grid);
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
      int sx = cx + (int)round(rr * sin(h.azRad)), sy = cy - (int)round(rr * cos(h.azRad));
      if (px >= 0) g.drawLine(px, py, sx, sy, gTheme.grid);
      px = sx; py = sy;
    }
  }

  // Stars (brightest first so labels favour them).
  for (int k = 0; k < kStarCount; ++k) {
    const Star& s = kStars[k];
    if (s.mag > _magLimit) continue;
    int sx, sy; float alt;
    if (!project(s, jd, latRad, lst, cx, cy, R, sx, sy, alt)) continue;
    int r = s.mag < 0.5f ? 3 : s.mag < 1.5f ? 2 : 1;
    g.fillCircle(sx, sy, r, gTheme.fg);
    if (_labels && s.mag <= 1.6f) {
      g.setTextDatum(textdatum_t::bottom_left);
      g.setTextColor(gTheme.dim, gTheme.bg);
      g.drawString(s.name, sx + 4, sy - 1);
    }
  }

  // Sun / Moon / planets, projected the same way (azimuthal). Distinct colours.
  for (int i = 0; _showSS && i < 9; ++i) {
    astro::PlanetState p = astro::planetState((astro::Planet)i, jd, _loc.active().lat, _loc.active().lon);
    if (!p.above) continue;
    double rr = R * (90.0 - p.elDeg) / 90.0;
    int sx = cx + (int)round(rr * sin(p.azDeg * astro::DEG2RAD));
    int sy = cy - (int)round(rr * cos(p.azDeg * astro::DEG2RAD));
    Color c = (i == 0) ? gTheme.warn : (i == 1) ? gTheme.fg : gTheme.ok;   // Sun / Moon / planets
    g.fillCircle(sx, sy, i <= 1 ? 3 : 2, c);
    if (_labels) {
      g.setTextDatum(textdatum_t::bottom_left);
      g.setTextColor(c, gTheme.bg);
      g.drawString(astro::planetName((astro::Planet)i), sx + 4, sy - 1);
    }
  }

  // Badge: magnitude limit.
  int by = cy0 + ch - 16;
  g.fillRect(4, by, 78, 14, gTheme.grid);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(gTheme.fg, gTheme.grid);
  g.setTextSize(1);
  g.drawString(String("mag<=") + String(_magLimit, 0) + (_labels ? " +lbl" : ""), 8, by + 7);
  // Badge: solar-system overlay toggle (bottom-right).
  g.fillRect(cw - 46, by, 42, 14, gTheme.grid);
  g.setTextColor(_showSS ? gTheme.ok : gTheme.dim, gTheme.grid);
  g.drawString(_showSS ? "SS on" : "SS off", cw - 42, by + 7);
}
