#include "PageSpaceWx.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../providers/SpaceWxProvider.h"
#include "../services/TimeService.h"
#include "../services/LocationService.h"
#include "../astro/Sun.h"
#include <time.h>

// 0 Poor, 1 Fair, 2 Good — simple HF heuristic from SFI/Kp + day/night.
static int bandCond(int meters, bool day, float kp, int sfi) {
  int score;
  if (meters <= 15)      score = (sfi >= 120 ? 2 : sfi >= 90 ? 1 : 0) - (day ? 0 : 1);
  else if (meters == 20) score = (sfi >= 100 ? 2 : 1);
  else                   score = day ? 0 : 2;   // 40/80 m favour night
  if (kp >= 5) score -= 2; else if (kp >= 4) score -= 1;
  return score < 0 ? 0 : score > 2 ? 2 : score;
}

void PageSpaceWx::tick(App& app, uint32_t nowMs) {
  if (!_dirty && nowMs - _lastDraw < 30000) return;
  _dirty = false; _lastDraw = nowMs;
  draw(app);
}

void PageSpaceWx::draw(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  g.fillRect(0, cy0, cw, ch, gTheme.bg);
  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(1);

  float kp = _wx.kp();
  int   sfi = _wx.sfi();
  if (kp < 0 && sfi < 0) {
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.setTextDatum(textdatum_t::middle_center);
    g.drawString(_wx.status() == ProviderStatus::Error ? "SWPC unavailable" : "loading space wx...",
                 cw / 2, cy0 + ch / 2);
    return;
  }

  int y = cy0 + 6;
  // Kp gauge.
  Color kc = kp >= 5 ? gTheme.warn : kp >= 4 ? gTheme.accent : gTheme.ok;
  const char* kl = kp >= 5 ? "STORM" : kp >= 4 ? "Active" : kp >= 3 ? "Unsettled" : "Quiet";
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.setTextSize(2); g.drawString(String("Kp ") + (kp < 0 ? String("?") : String(kp, 1)), 6, y);
  g.setTextSize(1); g.setTextColor(kc, gTheme.bg); g.drawString(kl, 120, y + 6);
  y += 22;
  int barW = cw - 12;
  g.drawRect(6, y, barW, 8, gTheme.grid);
  if (kp >= 0) g.fillRect(6, y, (int)(barW * (kp / 9.0)), 8, kc);
  y += 16;

  // SFI.
  const char* sl = sfi >= 150 ? "High" : sfi >= 100 ? "Moderate" : "Low";
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.setTextSize(2); g.drawString(String("SFI ") + (sfi < 0 ? String("?") : String(sfi)), 6, y);
  g.setTextSize(1); g.setTextColor(gTheme.accent, gTheme.bg); g.drawString(sl, 150, y + 6);
  y += 26;

  // Band table.
  bool day = true;
  if (_time.synced() && _loc.active().valid)
    day = astro::sunAltitudeDeg(_time.julianDate(), _loc.active().lat, _loc.active().lon) > -6;
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString("band", 6, y); g.drawString("day", 120, y); g.drawString("night", 200, y);
  y += 14;
  const int bands[] = {80, 40, 20, 15, 10};
  const char* names[] = {"Good", "Fair", "Poor"};
  Color cols[] = {gTheme.ok, gTheme.accent, gTheme.dim};
  for (int b : bands) {
    g.setTextColor(gTheme.fg, gTheme.bg);
    g.drawString(String(b) + "m", 6, y);
    int cd = bandCond(b, true, kp, sfi), cn = bandCond(b, false, kp, sfi);
    g.setTextColor(cols[2 - cd], gTheme.bg); g.drawString(names[2 - cd], 120, y);
    g.setTextColor(cols[2 - cn], gTheme.bg); g.drawString(names[2 - cn], 200, y);
    y += 13;
  }

  // Updated age.
  if (_wx.lastFetched()) {
    long age = (long)time(nullptr) - (long)_wx.lastFetched();
    g.setTextColor(_wx.status() == ProviderStatus::Stale ? gTheme.warn : gTheme.dim, gTheme.bg);
    g.setTextDatum(textdatum_t::bottom_left);
    g.drawString(String("updated ") + (age / 60) + "m ago", 6, cy0 + ch - 2);
    g.setTextDatum(textdatum_t::top_left);
  }
}
