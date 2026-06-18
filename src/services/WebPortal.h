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

  // main injects a filler for /api/status (heap, wifi, time, location) so the
  // portal stays decoupled from the services it reports on.
  void setStatusJsonProvider(std::function<void(JsonDocument&)> fn) { _statusFn = std::move(fn); }

  // Debug/automation hooks (spec §13): /api/screen.bmp (framebuffer read-back),
  // /api/tap?x&y and /api/swipe?dir to drive the UI remotely.
  void setDebug(App* app, Display* display) { _app = app; _display = display; }

private:
  Settings*      _s = nullptr;
  App*           _app = nullptr;
  Display*       _display = nullptr;
  AsyncWebServer _server{80};
  std::function<void(JsonDocument&)> _statusFn;
  File _up;                              // in-progress /api/fs upload target
};
