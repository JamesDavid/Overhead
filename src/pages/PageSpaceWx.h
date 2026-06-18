#pragma once
#include "../core/Page.h"
#include <Arduino.h>

class SpaceWxProvider;
class TimeService;
class LocationService;

// pages/PageSpaceWx — Space Weather / HF propagation (spec §6). Kp + SFI gauges,
// a day/night HF band table (simple heuristic), and the last-updated age.
class PageSpaceWx : public Page {
public:
  PageSpaceWx(SpaceWxProvider& wx, TimeService& time, LocationService& loc)
    : _wx(wx), _time(time), _loc(loc) {}

  const char* title() const override { return "Space Wx"; }
  void onEnter(App& app) override { _dirty = true; }
  void onData(App& app, ProviderId id) override { _dirty = true; }
  void tick(App& app, uint32_t nowMs) override;
  String gridStatus() override;

private:
  void draw(App& app);

  SpaceWxProvider& _wx;
  TimeService&     _time;
  LocationService& _loc;
  bool  _dirty = true;
  uint32_t _lastDraw = 0;
};
