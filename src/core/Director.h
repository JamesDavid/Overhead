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
class PageSatellites;

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
  void tick(uint32_t nowMs);

private:
  void scanPasses();
  int  ambientTarget();

  App*             _app = nullptr;
  Settings*        _s = nullptr;
  TimeService*     _time = nullptr;
  LocationService* _loc = nullptr;
  TleProvider*     _tle = nullptr;
  LaunchProvider*  _launch = nullptr;
  SpaceWxProvider* _spacewx = nullptr;
  PageSatellites*  _satPage = nullptr;

  astro::SatEngine _eng;
  String   _passBird;
  time_t   _passAos = 0;
  double   _passMaxEl = 0;
  String   _focusedBird;          // bird already pre-focused on the page

  uint32_t _lastScanMs = 0;
  uint32_t _lastDecideMs = 0;
};
