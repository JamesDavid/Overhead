#pragma once
#include <Arduino.h>
#include <functional>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>

class Settings;
class App;
class Display;

// services/WebPortal — the always-on LAN web UI (spec §3.1, §4): runtime
// settings page + JSON API + ElegantOTA browser firmware updates, on an
// ESPAsyncWebServer, with mDNS. Basic-auth guards both OTA and settings
// (spec §13). A real keyboard here is also how presets get added later
// (spec §6 Location) — no on-screen keyboard on resistive touch.
class WebPortal {
public:
  bool begin(Settings* s, const String& hostname);
  void loop();   // pump ElegantOTA (handles post-update reboot in async mode)

  // Stop/start the listener to reclaim contiguous heap for the feeds (the AsyncTCP/
  // server footprint). Routes stay registered — start() just re-opens the socket +
  // mDNS. Boots OFF by default (webOnBoot); re-enable from the Health "Web" toggle or
  // the serial console. NOTE: once a client has connected, stop() leaves that socket in
  // TIME_WAIT holding port 80, and AsyncTCP sets no SO_REUSEADDR, so an in-place
  // start() then fails to re-bind (logs "bind error: -8" but still reports running).
  // So a RE-enable after a stop must reboot for a clean bind — see everStopped().
  void stop();
  void start();
  bool running() const { return _running; }
  bool everStopped() const { return _everStopped; }   // a stop() happened -> re-bind needs a reboot

  // main injects a filler for /api/status (heap, wifi, time, location) so the
  // portal stays decoupled from the services it reports on.
  void setStatusJsonProvider(std::function<void(JsonDocument&)> fn) { _statusFn = std::move(fn); }

  // Debug/automation hooks (spec §13): /api/screen.bmp (framebuffer read-back),
  // /api/tap?x&y and /api/swipe?dir to drive the UI remotely.
  void setDebug(App* app, Display* display) { _app = app; _display = display; }

private:
  // --- async_tcp -> UI-thread marshalling -----------------------------------
  // Handlers run on the async_tcp task and must not touch Settings/Display/App
  // state the UI thread owns (the EventBus.h contract): POST /api/settings is
  // STAGED here and applied in loop(); GET /api/status + /api/settings serve
  // caches rebuilt in loop(). _lock is a mutex (not a critical section) so
  // String ops are legal while held.
  void applyPendingSettings();           // UI thread: merge a staged POST into Settings
  void refreshCaches(bool force);        // UI thread: rebuild the served JSON bodies

  Settings*      _s = nullptr;
  App*           _app = nullptr;
  Display*       _display = nullptr;
  AsyncWebServer _server{80};
  std::function<void(JsonDocument&)> _statusFn;
  File _up;                              // in-progress /api/fs upload target
  String _apiUser, _apiPass;             // Basic-auth creds gating the API (= OTA creds)
  String _host;                          // mDNS hostname (for restart)
  bool   _running = false;               // listener up?
  bool   _everStopped = false;           // a stop() happened this boot (re-bind needs a reboot)

  SemaphoreHandle_t _lock = nullptr;     // guards the staged POST + served caches
  String _pendingSet;                    // staged /api/settings POST body
  volatile bool _havePending = false;
  String _statusCache;                   // served by GET /api/status
  String _settingsCache;                 // served by GET /api/settings
  uint32_t _cacheMs = 0;
};
