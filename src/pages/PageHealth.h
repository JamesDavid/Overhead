#pragma once
#include "../core/Page.h"
#include <Arduino.h>

class Touch;
class TimeService;
class LocationService;
class TleProvider;
class LaunchProvider;
class AircraftProvider;
class SpaceWxProvider;
class WeatherProvider;
class ThemeController;
class Settings;
class WebPortal;

// pages/PageHealth — System / diagnostics (spec §6 Health). System info, a
// per-provider status table (status + age), and action buttons (force-refresh /
// recalibrate touch / reboot). No network of its own. (Spec wants this as a
// corner-glyph modal overlay; until the overlay chrome exists it rides the
// carousel as the last tab.)
class PageHealth : public Page {
public:
  PageHealth(Touch& touch, TimeService& time, LocationService& loc, String host,
             TleProvider& tle, LaunchProvider& launch, AircraftProvider& air,
             SpaceWxProvider& swx, WeatherProvider& wx,
             ThemeController& theme, Settings& settings)
    : _touch(touch), _time(time), _loc(loc), _host(std::move(host)),
      _tle(tle), _launch(launch), _air(air), _swx(swx), _wx(wx),
      _theme(theme), _settings(settings) {}

  const char* title() const override { return "Device Health"; }
  void setWebPortal(WebPortal* w) { _web = w; }   // for the "Web" on/off toggle
  void onEnter(App& app) override { _dirty = _needClear = true; }
  void onTouch(App& app, int x, int y) override;
  void tick(App& app, uint32_t nowMs) override;
  void invalidate() override { _dirty = true; }
  String gridStatus() override;          // worst current problem, or "ok"

private:
  void draw(App& app);
  void advanceRefresh();            // step the serialized force-refresh sequence (one feed at a time)
  bool anyFeedFetching() const;

  Touch&           _touch;
  TimeService&     _time;
  LocationService& _loc;
  String           _host;
  TleProvider&     _tle;
  LaunchProvider&  _launch;
  AircraftProvider& _air;
  SpaceWxProvider& _swx;
  WeatherProvider& _wx;
  ThemeController& _theme;
  Settings&        _settings;
  WebPortal*       _web = nullptr;   // optional: status-strip-free web-server toggle

  bool _dirty = true;
  bool _needClear = true;
  uint32_t _lastDraw = 0;
  bool _rebootArm = false;        // two-tap reboot confirm
  uint32_t _rebootArmMs = 0;
  uint32_t _refreshMs = 0;        // "refreshing..." toast
  int      _rstep = -1;           // serialized refresh: index of the next feed to trigger (-1 idle)
  uint32_t _rstepMs = 0;          // when the current feed was triggered (per-step 15s backstop)
};
