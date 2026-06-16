#include "ThemeController.h"
#include "Theme.h"
#include "../hal/Display.h"
#include "../services/TimeService.h"
#include "../services/LocationService.h"
#include "../services/Settings.h"
#include "../core/App.h"
#include "../astro/Sun.h"

void ThemeController::begin(TimeService* time, LocationService* loc, Display* display, Settings* settings, App* app) {
  _time = time; _loc = loc; _display = display; _settings = settings; _app = app;
  apply(false);                 // start in day until the first evaluation
}

void ThemeController::tick(uint32_t nowMs) {
  // Inactivity backlight dimming (cheap, every call) — wake on touch (spec §13).
  if (_app && _display) {
    uint32_t dimAfter = (uint32_t)_settings->getInt("dimAfterSec", 120) * 1000UL;
    int dimLevel = (int)_settings->getInt("dimLevel", 20);
    uint8_t target = (_app->idleMs(nowMs) > dimAfter)
                   ? (uint8_t)constrain(dimLevel, 5, (int)_baseBl) : _baseBl;
    if (target != _curBl) { _curBl = target; _display->setBacklight(target); }
  }

  if (_applied && nowMs - _lastMs < 15000) return;
  _lastMs = nowMs;

  String mode = _settings->getString("themeMode", "auto");
  bool night;
  if (mode == "day")        night = false;
  else if (mode == "night") night = true;
  else {                                          // auto, with hysteresis
    if (!_time->synced() || !_loc->active().valid) { apply(false); return; }
    double alt = astro::sunAltitudeDeg(_time->julianDate(),
                                       _loc->active().lat, _loc->active().lon);
    double thr = (double)_settings->getInt("themeNightAlt", -6);
    night = _night ? (alt < thr + 2.0)            // -> day only once well above
                   : (alt < thr);                 // -> night once below
  }
  apply(night);
}

void ThemeController::apply(bool night) {
  if (_applied && night == _night) return;
  _night = night;
  _applied = true;

  if (!night) {
    gTheme = themes::dark;
    _baseBl = 255;
  } else {
    bool red = _settings && _settings->getString("nightPalette", "dark") == "red";
    gTheme = red ? themes::redNight : themes::dark;
    int bl = _settings ? (int)_settings->getInt("nightBacklight", 90) : 90;
    _baseBl = (uint8_t)constrain(bl, 10, 255);
  }
  // Manual brightness override (Health page / web): 0 = follow day/night default.
  int forced = _settings ? (int)_settings->getInt("backlight", 0) : 0;
  if (forced > 0) _baseBl = (uint8_t)constrain(forced, 10, 255);
  _curBl = _baseBl;
  if (_display) _display->setBacklight(_baseBl);   // dimmer adjusts from here when idle
}
