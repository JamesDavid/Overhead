#pragma once
#include <Arduino.h>

class TimeService;
class LocationService;
class Display;
class Settings;
class App;

// core/ThemeController — drives the global palette + backlight from Sun altitude
// (spec §7.9), independent of the Director's attention logic. Auto mode flips
// day<->night with hysteresis so it won't flicker at the threshold; night uses
// the plain-dark or red dark-adapt palette and dims the backlight.
class ThemeController {
public:
  void begin(TimeService* time, LocationService* loc, Display* display, Settings* settings, App* app);
  void tick(uint32_t nowMs);     // theme every ~15 s; inactivity dim every call
  bool isNight() const { return _night; }
  void forceReapply() { _applied = false; _lastMs = 0; }   // re-evaluate on the next tick

private:
  void apply(bool night);

  TimeService*     _time = nullptr;
  LocationService* _loc = nullptr;
  Display*         _display = nullptr;
  Settings*        _settings = nullptr;
  App*             _app = nullptr;

  bool     _night = false;
  bool     _applied = false;
  uint8_t  _baseBl = 255;        // theme base backlight (before inactivity dim)
  uint8_t  _curBl = 255;         // actually-applied backlight
  uint32_t _lastMs = 0;
};
