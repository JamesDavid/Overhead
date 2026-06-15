#include "PageSolarSystem.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../services/TimeService.h"
#include "../services/LocationService.h"
#include "../services/Settings.h"
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
  // Bottom-left badge cycles the filter.
  if (x <= 80 && y >= app.contentH() - 20) { _filter = (_filter + 1) % 3; _dirty = true; return; }
  int third = app.contentW() / 3;
  if (x < third)          { do { _sel = (_sel - 1 + kN) % kN; } while (!visible(_sel) && _filter); }
  else if (x > 2 * third) { do { _sel = (_sel + 1) % kN; } while (!visible(_sel) && _filter); }
  _dirty = true;
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
    g.fillCircle(x, y, (i == _sel) ? 3 : 2, c);
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

  // Filter badge (bottom-left).
  const char* fl = _filter == 0 ? "all" : _filter == 1 ? "up" : "eye";
  int by = cy0 + ch - 16;
  g.fillRect(4, by, 72, 14, gTheme.grid);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(gTheme.fg, gTheme.grid);
  g.drawString(String("show ") + fl, 8, by + 7);
}
