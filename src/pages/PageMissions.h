#pragma once
#include "../core/Page.h"
#include <Arduino.h>

class TimeService;
class LocationService;
class MarsProvider;

// pages/PageMissions — "what's happening out there right now" (the inspire-a-kid
// page). Live Mars distance + one-way light-time + Mars in your sky (all computed),
// plus Perseverance/Curiosity mission sols (computed from landing) and the latest
// NASA rover status (sol/date/photos) when the feed is up. Works feed-down.
class PageMissions : public Page {
public:
  PageMissions(TimeService& time, LocationService& loc, MarsProvider& mars)
    : _time(time), _loc(loc), _mars(mars) {}

  const char* title() const override { return "Missions"; }
  void onEnter(App& app) override { _dirty = true; }
  void onData(App& app, ProviderId id) override { _dirty = true; }
  void tick(App& app, uint32_t nowMs) override;

private:
  void draw(App& app);

  TimeService&     _time;
  LocationService& _loc;
  MarsProvider&    _mars;
  bool  _dirty = true;
  uint32_t _lastDraw = 0;
};
