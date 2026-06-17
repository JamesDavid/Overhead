#pragma once
#include "../core/Page.h"
#include <Arduino.h>

class TimeService;
class LocationService;

// pages/PageStarMap — all-sky star chart (spec §6, stretch). Azimuthal projection
// (zenith centre, horizon edge), stars sized by magnitude, major constellation
// lines, with a magnitude-limit filter (bottom-left badge) and a labels toggle
// (centre tap). Prototype catalog in assets/StarCatalog.h.
class PageStarMap : public Page {
public:
  PageStarMap(TimeService& time, LocationService& loc) : _time(time), _loc(loc) {}

  const char* title() const override { return "Star Map"; }
  void onEnter(App& app) override { _dirty = true; }
  void onData(App& app, ProviderId id) override { _dirty = true; }
  void onTouch(App& app, int x, int y) override;
  void tick(App& app, uint32_t nowMs) override;
  bool autoAdvance(App& app) override;

private:
  void draw(App& app);

  TimeService&     _time;
  LocationService& _loc;
  float _magLimit = 3.0f;
  bool  _labels = true;
  bool  _showSS = true;          // overlay Sun/Moon/planets + ecliptic
  bool  _dirty = true;
  uint32_t _lastDraw = 0;
};
