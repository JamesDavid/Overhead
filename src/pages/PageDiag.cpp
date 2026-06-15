#include "PageDiag.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../hal/Board.h"
#include "../services/TimeService.h"
#include "../services/LocationService.h"
#include <WiFi.h>
#include <time.h>

void PageDiag::onTouch(App& app, int x, int y) {
  Serial.println("[diag] tap -> refreshing location");
  _loc.refresh();
  _dirty = true;
}

void PageDiag::tick(App& app, uint32_t nowMs) {
  // Redraw on data changes, and once a second so the clock/uptime advance.
  if (!_dirty && nowMs - _lastDrawMs < 1000) return;
  _dirty = false;
  _lastDrawMs = nowMs;
  draw(app);
}

void PageDiag::draw(App& app) {
  auto& g = app.display().gfx();
  const int x0 = 8;
  int y = app.contentY() + 6;
  g.fillRect(0, app.contentY(), app.contentW(), app.contentH(), gTheme.bg);
  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(1);

  auto line = [&](const String& s, Color c) {
    g.setTextColor(c, gTheme.bg);
    g.drawString(s, x0, y);
    y += 14;
  };

  g.setTextColor(gTheme.accent, gTheme.bg);
  g.setTextSize(2);
  g.drawString("Overhead \xB7 M3", x0, y); y += 22;
  g.setTextSize(1);

  line(String("board:  ") + BOARD_NAME, gTheme.dim);

  // WiFi
  if (WiFi.status() == WL_CONNECTED) {
    line(String("wifi:   ") + WiFi.SSID() + "  " + WiFi.RSSI() + "dBm", gTheme.ok);
    line(String("ip:     ") + WiFi.localIP().toString(), gTheme.fg);
    line(String("web:    http://") + _host + ".local/  (/update)", gTheme.fg);
  } else {
    line("wifi:   offline", gTheme.warn);
  }

  // Time
  time_t now = time(nullptr);
  if (_time.synced() && now > 1600000000) {
    struct tm tm; localtime_r(&now, &tm);
    char buf[32]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    line(String("time:   ") + buf, gTheme.fg);
  } else {
    line("time:   not synced (astro gated)", gTheme.warn);
  }

  // Location
  const auto& loc = _loc.active();
  if (loc.valid) {
    char ll[48]; snprintf(ll, sizeof(ll), "%.4f, %.4f", loc.lat, loc.lon);
    line(String("loc:    ") + loc.name + "  " + ll, gTheme.fg);
    line(String("tz:     ") + (loc.tzOffsetSec / 3600.0) + "h", gTheme.dim);
  } else {
    line("loc:    resolving…", gTheme.warn);
  }

  // Memory
  line(String("heap:   ") + Display::freeHeap() + "  blk " + Display::largestFreeBlock(), gTheme.dim);
  line(String("uptime: ") + (millis() / 1000) + "s", gTheme.dim);

  y += 6;
  line("tap anywhere to re-resolve location", gTheme.accent);
}
