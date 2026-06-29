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
#include "../services/WebPortal.h"
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

// Compact duration: the two most-significant non-zero units (e.g. "2d3h", "5h12m",
// "3m07s", "45s"). Keeps the narrow age column readable instead of huge second counts.
static void fmtDur(uint32_t s, char* out, size_t n) {
  uint32_t d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60, sec = s % 60;
  if (d)      snprintf(out, n, "%lud%luh", (unsigned long)d, (unsigned long)h);
  else if (h) snprintf(out, n, "%luh%lum", (unsigned long)h, (unsigned long)m);
  else if (m) snprintf(out, n, "%lum%02lus", (unsigned long)m, (unsigned long)sec);
  else        snprintf(out, n, "%lus", (unsigned long)sec);
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

// Manual brightness cycle: Auto (follow day/night) / 25 / 50 / 75 / 100 %.
static const int   kBri[]     = {0, 64, 128, 192, 255};
static const char* kBriName[] = {"Auto", "25%", "50%", "75%", "100%"};
static int briIndex(Settings& s) {
  int b = (int)s.getInt("backlight", 0);
  for (int i = 0; i < 5; ++i) if (kBri[i] == b) return i;
  return 0;
}

void PageHealth::onTouch(App& app, int x, int y) {
  const int u = app.ui();
  if (y >= app.contentH() - 44 * u && y < app.contentH() - 26 * u) {   // display / brightness / shots / web row
    int col = x / (app.contentW() / 6);
    if (col == 0) {                                            // cycle palette
      setThemeMode(_settings, (themeModeIndex(_settings) + 1) % 4);
      _theme.forceReapply();
    } else if (col == 1) {                                     // cycle brightness
      _settings.set("backlight", (long)kBri[(briIndex(_settings) + 1) % 5]);
      _settings.save(); _theme.forceReapply();
    } else if (col == 2) {                                     // toggle remote screenshots
      bool on = !app.display().shotsEnabled();
      app.display().setShotsEnabled(on);
      _settings.set("debugShots", on); _settings.save();
    } else if (col == 3 && _web) {                             // toggle the web server (frees heap)
      if (_web->running()) {                                   // ON -> OFF: live stop frees heap now
        _web->stop();
        _settings.set("webOnBoot", false); _settings.save();
      } else if (_web->everStopped()) {                        // re-enable after a stop: a client left
        _settings.set("webOnBoot", true); _settings.save();    // :80 in TIME_WAIT, so reboot to re-bind
        delay(150); ESP.restart();                             // cleanly (in-place start() would -8)
      } else {                                                 // first enable this boot: clean start
        _web->start();
        _settings.set("webOnBoot", true); _settings.save();
      }
    } else if (col == 4) {                                     // cycle rotation (for CYD variants)
      // All 8: variants differ in which parity is landscape (320x240-native -> even, 240x320-native
      // -> odd, e.g. the Elegoo USB-C board), so step through everything and stop when it looks right.
      int next = (app.display().rotation() + 1) & 7;
      _settings.set("dispRotation", (long)next); _settings.save();
      app.display().applyDisplayPrefs(next, _settings.getBool("dispInvert"));
      _touch.calibrate(app.display());                         // touch cal is rotation-specific -> recal now
    } else if (col == 5) {                                     // toggle colour invert
      bool iv = !_settings.getBool("dispInvert");
      _settings.set("dispInvert", iv); _settings.save();
      app.display().applyDisplayPrefs(_settings.getBool("dispMirror"), iv);
    }
    _dirty = _needClear = true;
    return;
  }
  if (y < app.contentH() - 24 * u) return;       // only the bottom button row
  int col = x / (app.contentW() / 3);
  if (col == 0) {                                 // Refresh all -- serialized, one feed at a time
    _rstep = 0; _rstepMs = 0;                     // advanceRefresh() (tick) walks the sequence
    _refreshMs = millis();
  } else if (col == 1) {                          // Recalibrate touch
    _touch.calibrate(app.display());
  } else {                                        // Reboot (two-tap confirm)
    if (_rebootArm && millis() - _rebootArmMs < 4000) { delay(150); ESP.restart(); }
    else { _rebootArm = true; _rebootArmMs = millis(); }
  }
  _dirty = _needClear = true;
}

void PageHealth::tick(App& app, uint32_t nowMs) {
  if (_rstep >= 0) advanceRefresh();
  uint32_t cadence = (_rstep >= 0) ? 200 : 1000;   // feeds light up live during a refresh sequence
  if (!_dirty && nowMs - _lastDraw < cadence) return;
  _dirty = false; _lastDraw = nowMs;
  draw(app);
}

// Force-refresh the providers one at a time: trigger a feed, wait for it to finish (15s backstop),
// then the next. Serializing keeps the 8-slot NetClient queue from overflowing -- TLE fires 3
// requests and SpaceWx 5, so an all-at-once refresh would drop the later feeds (e.g. Launches).
void PageHealth::advanceRefresh() {
  if (anyFeedFetching() && millis() - _rstepMs < 15000) return;
  switch (_rstep) {
    case 0: _tle.refresh(true);    break;
    case 1: _launch.refresh(true); break;
    case 2: _air.poll();           break;
    case 3: _swx.refresh(true);    break;
    case 4: _wx.refresh(true);     break;
    default: _rstep = -1; return;                  // sequence complete
  }
  _rstep++; _rstepMs = millis(); _dirty = true;
}

bool PageHealth::anyFeedFetching() const {
  return _tle.fetching() || _launch.fetching() || _air.fetching() || _swx.fetching() || _wx.fetching();
}

String PageHealth::gridStatus() {
  String blk = String("blk ") + (Display::largestFreeBlock() / 1024) + "k";   // largest contiguous block
  String pre;                                                                  // problem on its own line above blk
  if (WiFi.status() != WL_CONNECTED)            pre = "WiFi down";
  else if (!_time.synced())                     pre = "no time";
  else {
    int err = (_tle.status()    == ProviderStatus::Error) + (_launch.status() == ProviderStatus::Error)
            + (_air.status()    == ProviderStatus::Error) + (_swx.status()    == ProviderStatus::Error)
            + (_wx.status()     == ProviderStatus::Error);
    if (err) pre = String(err) + (err == 1 ? " error" : " errors");
  }
  return pre.length() ? pre + "\n" + blk : blk;                                // '\n' forces a new line
}

void PageHealth::draw(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  if (_needClear) { g.fillRect(0, cy0, cw, ch, gTheme.bg); _needClear = false; }

  const int u = app.ui();
  int x0 = 6 * u, y = cy0 + 4 * u;
  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(u);
  auto line = [&](const String& s, Color c) { g.setTextColor(c, gTheme.bg); g.drawString(padRight(s, 40), x0, y); y += 12 * u; };

  g.setTextColor(gTheme.accent, gTheme.bg);
  g.setTextSize(2 * u); g.drawString("Health", x0, y); y += 20 * u;
  g.setTextSize(u);

  // System.
  if (WiFi.status() == WL_CONNECTED)
    line(String("wifi ") + WiFi.SSID() + " " + WiFi.RSSI() + "dBm  " + WiFi.localIP().toString(), gTheme.fg);
  else line("wifi offline", gTheme.warn);
  line(String("mdns http://") + _host + ".local  /update", gTheme.dim);
  line(String("heap ") + Display::freeHeap() + "  blk " + Display::largestFreeBlock() +
       "  psram " + Display::psramSize(), gTheme.dim);
  size_t fsTot = LittleFS.totalBytes(), fsUse = LittleFS.usedBytes();
  line(String("fs ") + (fsUse / 1024) + "/" + (fsTot / 1024) + " KB  fw " + OVERHEAD_FW_VERSION, gTheme.dim);
  char up[12]; fmtDur(millis() / 1000, up, sizeof(up));
  line(String("uptime ") + up + "  ntp " + (_time.synced() ? "ok" : "no"), gTheme.dim);
  const auto& loc = _loc.active();
  if (loc.valid) { char b[48]; snprintf(b, sizeof(b), "loc %s  %.3f,%.3f", loc.name.c_str(), loc.lat, loc.lon); line(b, gTheme.dim); }

  y += 2 * u;
  g.setTextColor(gTheme.fg, gTheme.bg); g.drawString("provider        status    age", x0, y); y += 12 * u;
  uint32_t now = (uint32_t)time(nullptr);
  auto prow = [&](const char* name, ProviderStatus st, uint32_t fetched, bool busy, int idx) {
    bool queued = (_rstep >= 0 && idx >= _rstep && !busy);   // waiting its turn in the refresh sequence
    const char* tok = busy ? "fetching" : queued ? "queued" : statusStr(st);
    Color c = busy ? gTheme.accent : queued ? gTheme.dim
            : st == ProviderStatus::Ready ? gTheme.ok : st == ProviderStatus::Error ? gTheme.warn : gTheme.dim;
    char age[12] = "-";
    if (!busy && !queued && fetched > 1600000000UL && now > fetched) fmtDur(now - fetched, age, sizeof(age));  // ignore pre-NTP stamps
    char b[48]; snprintf(b, sizeof(b), "%-14s  %-8s  %s", name, tok, (busy || queued) ? "" : age);
    g.setTextColor(c, gTheme.bg); g.drawString(padRight(b, 40), x0, y); y += 12 * u;
  };
  prow("TLE",      _tle.status(),    _tle.lastFetched(),    _tle.fetching(),    0);
  prow("Launch",   _launch.status(), _launch.lastFetched(), _launch.fetching(), 1);
  prow("Aircraft", _air.status(),    _air.lastFetched(),    _air.fetching(),    2);
  prow("SpaceWx",  _swx.status(),    _swx.lastFetched(),    _swx.fetching(),    3);
  prow("Weather",  _wx.status(),     _wx.lastFetched(),     _wx.fetching(),     4);

  if (_rstep >= 0) {                              // live while the serialized refresh runs
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(gTheme.ok, gTheme.bg);
    g.drawString("refreshing data sources...", cw / 2, cy0 + ch - 52 * u);
  }

  // Display / brightness / screenshots / web / mirror / invert buttons (tap to cycle/toggle).
  int ty = cy0 + ch - 44 * u, tw = cw / 6, bh = 18 * u;
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawRect(2 * u, ty, tw - 3 * u, bh, gTheme.grid);
  g.drawString(String("Disp:") + kThemeNames[themeModeIndex(_settings)], tw / 2, ty + 9 * u);
  g.drawRect(tw + 2 * u, ty, tw - 3 * u, bh, gTheme.grid);
  g.drawString(String("Bri:") + kBriName[briIndex(_settings)], tw + tw / 2, ty + 9 * u);
  g.drawRect(2 * tw + 2 * u, ty, tw - 3 * u, bh, gTheme.grid);
  g.drawString(String("Shot:") + (app.display().shotsEnabled() ? "on" : "off"), 2 * tw + tw / 2, ty + 9 * u);
  g.drawRect(3 * tw + 2 * u, ty, tw - 3 * u, bh, gTheme.grid);     // web server on/off (frees heap for feeds)
  bool webOn = !_web || _web->running();
  g.setTextColor(webOn ? gTheme.accent : gTheme.warn, gTheme.bg);
  g.drawString(String("Web:") + (webOn ? "on" : "off"), 3 * tw + tw / 2, ty + 9 * u);
  g.drawRect(4 * tw + 2 * u, ty, tw - 3 * u, bh, gTheme.grid);     // rotation (cycle landscape) / invert
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString(String("Rot:") + app.display().rotation(), 4 * tw + tw / 2, ty + 9 * u);
  g.drawRect(5 * tw + 2 * u, ty, tw - 4 * u, bh, gTheme.grid);
  g.setTextColor(_settings.getBool("dispInvert") ? gTheme.accent : gTheme.dim, gTheme.bg);
  g.drawString("Invert", 5 * tw + tw / 2, ty + 9 * u);
  g.setTextColor(gTheme.accent, gTheme.bg);

  // Button row.
  int by = cy0 + ch - 22 * u, bw = cw / 3;
  bool arm = _rebootArm && millis() - _rebootArmMs < 4000;
  const char* labels[] = {"Refresh", "Calibrate", arm ? "Reboot?" : "Reboot"};
  for (int i = 0; i < 3; ++i) {
    g.drawRect(i * bw + 2 * u, by, bw - 4 * u, 18 * u, gTheme.grid);
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(i == 2 && arm ? gTheme.warn : gTheme.accent, gTheme.bg);
    g.drawString(labels[i], i * bw + bw / 2, by + 9 * u);
  }
}
