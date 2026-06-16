#include "PageHealth.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../hal/Touch.h"
#include "../hal/Board.h"
#include "../services/TimeService.h"
#include "../services/LocationService.h"
#include "../providers/TleProvider.h"
#include "../providers/LaunchProvider.h"
#include "../providers/AircraftProvider.h"
#include "../providers/SpaceWxProvider.h"
#include "../providers/WeatherProvider.h"
#include "../core/ThemeController.h"
#include "../services/Settings.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <time.h>

static const char* statusStr(ProviderStatus s) {
  switch (s) {
    case ProviderStatus::Ready:   return "ready";
    case ProviderStatus::Stale:   return "stale";
    case ProviderStatus::Error:   return "error";
    default:                      return "load";
  }
}

// Display-mode cycle: Auto / Day / Night (dark) / Night (red dark-adapt).
static const char* kThemeNames[] = {"Auto", "Day", "Night", "Red"};
static int themeModeIndex(Settings& s) {
  String m = s.getString("themeMode", "auto");
  if (m == "day")   return 1;
  if (m == "night") return s.getString("nightPalette", "dark") == "red" ? 3 : 2;
  return 0;
}
static void setThemeMode(Settings& s, int idx) {
  switch (idx) {
    case 1: s.set("themeMode", "day");   break;
    case 2: s.set("themeMode", "night"); s.set("nightPalette", "dark"); break;
    case 3: s.set("themeMode", "night"); s.set("nightPalette", "red");  break;
    default: s.set("themeMode", "auto"); break;
  }
  s.save();
}

void PageHealth::onTouch(App& app, int x, int y) {
  if (y >= app.contentH() - 44 && y < app.contentH() - 26) {   // display-mode button
    setThemeMode(_settings, (themeModeIndex(_settings) + 1) % 4);
    _theme.forceReapply();                        // apply the new palette immediately
    _dirty = _needClear = true;
    return;
  }
  if (y < app.contentH() - 24) return;           // only the bottom button row
  int col = x / (app.contentW() / 3);
  if (col == 0) {                                 // Refresh all
    _tle.refresh(true); _launch.refresh(true); _air.poll();
    _swx.refresh(true); _wx.refresh(true);
  } else if (col == 1) {                          // Recalibrate touch
    _touch.calibrate(app.display());
  } else {                                        // Reboot
    delay(150); ESP.restart();
  }
  _dirty = _needClear = true;
}

void PageHealth::tick(App& app, uint32_t nowMs) {
  if (!_dirty && nowMs - _lastDraw < 1000) return;
  _dirty = false; _lastDraw = nowMs;
  draw(app);
}

void PageHealth::draw(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  if (_needClear) { g.fillRect(0, cy0, cw, ch, gTheme.bg); _needClear = false; }

  int x0 = 6, y = cy0 + 4;
  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(1);
  auto line = [&](const String& s, Color c) { g.setTextColor(c, gTheme.bg); g.drawString(padRight(s, 40), x0, y); y += 12; };

  g.setTextColor(gTheme.accent, gTheme.bg);
  g.setTextSize(2); g.drawString("Health", x0, y); y += 20;
  g.setTextSize(1);

  // System.
  if (WiFi.status() == WL_CONNECTED)
    line(String("wifi ") + WiFi.SSID() + " " + WiFi.RSSI() + "dBm  " + WiFi.localIP().toString(), gTheme.fg);
  else line("wifi offline", gTheme.warn);
  line(String("mdns http://") + _host + ".local  /update", gTheme.dim);
  line(String("heap ") + Display::freeHeap() + "  blk " + Display::largestFreeBlock() +
       "  psram " + Display::psramSize(), gTheme.dim);
  size_t fsTot = LittleFS.totalBytes(), fsUse = LittleFS.usedBytes();
  line(String("fs ") + (fsUse / 1024) + "/" + (fsTot / 1024) + " KB  fw " + OVERHEAD_FW_VERSION, gTheme.dim);
  line(String("uptime ") + (millis() / 1000) + "s  ntp " + (_time.synced() ? "ok" : "no"), gTheme.dim);
  const auto& loc = _loc.active();
  if (loc.valid) { char b[48]; snprintf(b, sizeof(b), "loc %s  %.3f,%.3f", loc.name.c_str(), loc.lat, loc.lon); line(b, gTheme.dim); }

  y += 2;
  g.setTextColor(gTheme.fg, gTheme.bg); g.drawString("provider        status   age", x0, y); y += 12;
  uint32_t now = (uint32_t)time(nullptr);
  auto prow = [&](const char* name, ProviderStatus st, uint32_t fetched) {
    Color c = st == ProviderStatus::Ready ? gTheme.ok : st == ProviderStatus::Error ? gTheme.warn : gTheme.dim;
    char age[12] = "-";
    if (fetched && now > fetched) snprintf(age, sizeof(age), "%lus", (unsigned long)(now - fetched));
    char b[48]; snprintf(b, sizeof(b), "%-14s  %-7s  %s", name, statusStr(st), age);
    g.setTextColor(c, gTheme.bg); g.drawString(padRight(b, 40), x0, y); y += 12;
  };
  prow("TLE",      _tle.status(),    _tle.lastFetched());
  prow("Launch",   _launch.status(), _launch.lastFetched());
  prow("Aircraft", _air.status(),    _air.lastFetched());
  prow("SpaceWx",  _swx.status(),    _swx.lastFetched());
  prow("Weather",  _wx.status(),     _wx.lastFetched());

  // Display-mode button (tap to cycle palette), above the action buttons.
  int ty = cy0 + ch - 44;
  g.drawRect(2, ty, cw - 4, 18, gTheme.grid);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString(String("Display: ") + kThemeNames[themeModeIndex(_settings)] + "  (tap)", cw / 2, ty + 9);

  // Button row.
  int by = cy0 + ch - 22, bw = cw / 3;
  const char* labels[] = {"Refresh", "Calibrate", "Reboot"};
  for (int i = 0; i < 3; ++i) {
    g.drawRect(i * bw + 2, by, bw - 4, 18, gTheme.grid);
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(gTheme.accent, gTheme.bg);
    g.drawString(labels[i], i * bw + bw / 2, by + 9);
  }
}
