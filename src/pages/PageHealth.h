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

  const char* title() const override { return "Health"; }
  void onEnter(App& app) override { _dirty = _needClear = true; }
  void onTouch(App& app, int x, int y) override;
  void tick(App& app, uint32_t nowMs) override;

private:
  void draw(App& app);

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

  bool _dirty = true;
  bool _needClear = true;
  uint32_t _lastDraw = 0;
};
