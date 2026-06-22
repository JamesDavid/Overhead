#pragma once
#include "../core/Page.h"
#include "../astro/SatEngine.h"
#include <Arduino.h>
#include <vector>

class TimeService;
class LocationService;
class WeatherProvider;
class TleProvider;
class LaunchProvider;
class Settings;

// pages/PageAgenda — Today/Tonight agenda (spec §6). The Sky Window timeline
// (darkness/twilight + moon-up + cloud heat + event markers) over the next ~24 h,
// a merged pass/launch event list with a per-event cloud glyph, and a one-line
// "worth it?" verdict. Daytime ambient default. Synthesises SatEngine +
// LaunchProvider + astro Sun/Moon + WeatherProvider.
class PageAgenda : public Page {
public:
  PageAgenda(TimeService& time, LocationService& loc, WeatherProvider& wx,
             TleProvider& tle, LaunchProvider& launch, Settings& settings)
    : _time(time), _loc(loc), _wx(wx), _tle(tle), _launch(launch), _settings(settings) {}

  const char* title() const override { return "Agenda"; }
  bool clockKeepLive() const override { return true; }   // countdowns keep ticking under the clock
  void onEnter(App& app) override { _dirty = true; }
  // Redraw on data, but only force the EXPENSIVE recompute (pass prediction) when
  // the location changes — otherwise it runs on every provider publish and starves
  // the UI/touch loop.
  void onData(App& app, ProviderId id) override { _dirty = true; if (id == ProviderId::Location) _computed = false; }
  void onTouch(App& app, int x, int y) override;   // tap an event -> jump to its tab
  void onScroll(App& app, int dy) override;        // swipe up/down -> scroll Upcoming list
  void tick(App& app, uint32_t nowMs) override;
  void invalidate() override { _dirty = true; }
  // SRAM tiling: tick() only updates + flags; render() is the pure redraw the App tiles off-PSRAM.
  bool tiled() const override { return true; }
  bool needsDraw() const override { return _needsDraw; }
  void clearNeedsDraw() override { _needsDraw = false; }
  void render(App& app) override;
  String gridStatus() override;                    // next upcoming event + countdown

private:
  static constexpr int kHours = 24;
  // ref = focus hint for the target page (satellite name / launch id); kind: 0 pass,
  // 1 launch, 2 sun/moon.
  struct Event { time_t t; String label; uint8_t kind; String ref; };

  void recompute();
  void draw(App& app);
  void jumpToEvent(App& app, int i);   // switch to the event's tab

  TimeService&     _time;
  LocationService& _loc;
  WeatherProvider& _wx;
  TleProvider&     _tle;
  LaunchProvider&  _launch;
  Settings&        _settings;

  astro::SatEngine _eng;
  float _sunAlt[kHours];
  int8_t _cloud[kHours];
  bool  _moonUp[kHours];
  std::vector<Event> _events;
  String _verdict;
  time_t _base = 0;

  bool  _dirty = true;
  bool  _needsDraw = false;          // SRAM tiling: render() owes a redraw (set by tick, cleared by App)
  bool  _computed = false;
  uint32_t _lastRecompute = 0;
  uint32_t _lastDraw = 0;
  int   _listY0 = 0, _listN = 0;     // upcoming-list geometry for tap-to-jump
  int   _listScroll = 0;             // first Upcoming event shown (vertical-swipe scroll)
};
