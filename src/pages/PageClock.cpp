#include "PageClock.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../services/TimeService.h"
#include "../services/LocationService.h"
#include "../services/Settings.h"
#include "../astro/SolarSystem.h"
#include <time.h>
#include <math.h>

static const char* kBg[] = {"plain", "stars", "rings"};
static constexpr int kBgN = 3;

void PageClock::onEnter(App& app) {
  _mode = (int)_settings.getInt("clockBg", 0);
  _dirty = true; _lastMin = -1;
  app.setPin(true);        // rest here; the Director won't tour away
}
void PageClock::onExit(App& app) { app.setPin(false); }

void PageClock::onTouch(App& app, int x, int y) {
  if (y >= app.contentH() - 18) {                    // bottom chip: cycle the background
    _mode = (_mode + 1) % kBgN;
    _settings.set("clockBg", (long)_mode); _settings.save();
    _dirty = true;
  }
}

void PageClock::tick(App& app, uint32_t nowMs) {
  time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
  if (!_dirty && tm.tm_min == _lastMin) return;      // calm: redraw on the minute
  _lastMin = tm.tm_min; _dirty = false; _lastDraw = nowMs;
  draw(app);
}

void PageClock::drawBackground(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  if (_mode == 1) {                                  // decorative starfield (not a sky chart)
    uint32_t s = 2463534242u;
    for (int i = 0; i < 70; ++i) {
      s = s * 1103515245u + 12345u; int x = (int)((s >> 9) % cw);
      s = s * 1103515245u + 12345u; int y = cy0 + (int)((s >> 9) % ch);
      uint8_t b = 50 + (uint8_t)((s >> 5) % 170);
      g.drawPixel(x, y, gTheme.mono ? rgb565(b, b / 5, 0) : rgb565(b, b, b));
    }
  } else if (_mode == 2) {                            // concentric orbit rings (orrery-ish)
    int cx = cw / 2, cy = cy0 + ch / 2;
    for (int r = 28; r < cw; r += 28) g.drawCircle(cx, cy, r, gTheme.grid);
    g.fillCircle(cx, cy, 3, gTheme.warn);
  }
}

void PageClock::draw(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  g.startWrite();
  g.fillRect(0, cy0, cw, ch, gTheme.bg);
  drawBackground(app);

  time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
  g.setTextDatum(textdatum_t::middle_center);
  char hm[8]; strftime(hm, sizeof(hm), "%H:%M", &tm);
  g.setTextColor(gTheme.fg, gTheme.bg);              // opaque: sits cleanly over the bg
  g.setTextSize(6); g.drawString(hm, cw / 2, cy0 + ch / 2 - 18);
  char dt[24]; strftime(dt, sizeof(dt), "%a %b %d", &tm);
  g.setTextSize(2); g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString(dt, cw / 2, cy0 + ch / 2 + 22);

  if (_loc.active().valid && _time.synced()) {        // sun/moon glance
    double jd = _time.julianDate();
    int illum = (int)round(astro::moonIlluminationPct(jd));
    astro::PlanetState su = astro::planetState(astro::Planet::Sun, jd, _loc.active().lat, _loc.active().lon);
    char b[40]; snprintf(b, sizeof(b), "sun %d\xF7   moon %d%%", (int)round(su.elDeg), illum);
    g.setTextSize(1); g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(b, cw / 2, cy0 + ch / 2 + 44);
  }

  g.fillRect(cw / 2 - 32, cy0 + ch - 16, 64, 14, gTheme.grid);   // background-mode chip
  g.setTextSize(1); g.setTextColor(gTheme.fg, gTheme.grid);
  g.drawString(String("bg: ") + kBg[_mode], cw / 2, cy0 + ch - 9);
  g.endWrite();
}
