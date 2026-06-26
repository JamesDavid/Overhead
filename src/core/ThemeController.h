#pragma once
#include <Arduino.h>

class TimeService;
class LocationService;
class Display;
class Settings;
class App;

// core/ThemeController — drives the global palette + backlight from Sun altitude
// (spec §7.9), independent of the Director's attention logic. Auto mode steps through
// THREE tiers by sun altitude with hysteresis (so it won't flicker at a threshold):
//   Day  (daylight)          -> dark palette, full backlight
//   Night(twilight/dusk)     -> dark palette, dimmed backlight
//   Red  (astronomical dark) -> red dark-adapt palette (if enabled), dimmed backlight
// Forced modes (Health/web) pin a single tier.
class ThemeController {
public:
  enum Tier { TierDay = 0, TierNight = 1, TierRed = 2 };

  void begin(TimeService* time, LocationService* loc, Display* display, Settings* settings, App* app);
  void tick(uint32_t nowMs);     // theme every ~15 s; inactivity dim every call
  bool isNight() const { return _tier != TierDay; }
  void forceReapply() { _applied = false; _lastMs = 0; }   // re-evaluate on the next tick

private:
  void apply(Tier tier);

  TimeService*     _time = nullptr;
  LocationService* _loc = nullptr;
  Display*         _display = nullptr;
  Settings*        _settings = nullptr;
  App*             _app = nullptr;

  Tier     _tier = TierDay;
  bool     _applied = false;
  uint8_t  _baseBl = 255;        // theme base backlight (before inactivity dim)
  uint8_t  _curBl = 255;         // actually-applied backlight
  uint32_t _lastMs = 0;
};
