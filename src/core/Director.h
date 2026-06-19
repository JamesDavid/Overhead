#pragma once
#include <Arduino.h>
#include <time.h>
#include "../astro/SatEngine.h"

class App;
class Settings;
class TimeService;
class LocationService;
class TleProvider;
class LaunchProvider;
class SpaceWxProvider;
class AviationWxProvider;
class WeatherProvider;
class PageSatellites;
class PageAviation;

// core/Director — Intelligent Focus (spec §7). The only component with cross-tab
// awareness: it reads providers + clock + Sun and drives App::autoFocus(). Two
// layers — an ambient resting tab chosen by day/night, and interrupts (imminent
// sat pass / launch) that seize focus. When AUTO it switches the carousel; when
// MANUAL/pinned it raises a tab badge instead. Pass scanning is limited to
// watchlisted birds and cached (spec §13: pass prediction is the heavy math).
class Director {
public:
  void begin(App* app, Settings* s, TimeService* time, LocationService* loc,
             TleProvider* tle, LaunchProvider* launch, PageSatellites* satPage);
  void setSpaceWx(SpaceWxProvider* wx) { _spacewx = wx; }
  void setAviation(AviationWxProvider* a) { _avwx = a; }
  void setAviationPage(PageAviation* p) { _avPage = p; }
  void setWeather(WeatherProvider* w) { _wx = w; }
  void tick(uint32_t nowMs);

private:
  void scanPasses();

  App*             _app = nullptr;
  Settings*        _s = nullptr;
  TimeService*     _time = nullptr;
  LocationService* _loc = nullptr;
  TleProvider*     _tle = nullptr;
  LaunchProvider*  _launch = nullptr;
  SpaceWxProvider* _spacewx = nullptr;
  AviationWxProvider* _avwx = nullptr;
  WeatherProvider* _wx = nullptr;
  PageSatellites*  _satPage = nullptr;
  PageAviation*    _avPage = nullptr;

  astro::SatEngine _eng;
  String   _passBird;
  time_t   _passAos = 0;
  time_t   _passLos = 0;
  double   _passMaxEl = 0;
  bool     _passVisible = false;  // bright sat, sunlit at TCA, observer in darkness
  bool     _passRadio = false;    // amateur/radio-workable satellite
  int      _passCloud = -1;       // cloud cover % at TCA (-1 unknown)
  String   _focusedBird;          // bird already pre-focused on the page

  uint32_t _lastScanMs = 0;
  uint32_t _lastDecideMs = 0;
  uint32_t _lastTourMs = 0;       // attract-tour dwell clock
  bool     _ambNight = false;     // last day/night state (reset rotation on flip)
  int      _ambPos = 0;           // index into the ambient page rotation
  String   _avWxKey;              // last aviation-notice key (rising-edge announce)
  String   _avAlertMsg;           // pending weather announcement text
  uint32_t _avAlertUntil = 0;     // show _avAlertMsg until this millis()
};
