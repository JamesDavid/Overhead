#pragma once
#include "../core/Page.h"
#include <Arduino.h>

class AircraftProvider;
class LocationService;
class Settings;

// pages/PageAircraft — the Aircraft tab (spec §6). Observer-centred radar (N-up,
// range rings), heading chevrons, and a selected-contact detail panel. Tap
// left/right to step through contacts (sorted by range). loading/empty/error.
// Deferred (backlog): nearest-airport + likely-frequency (needs bundled
// OurAirports subset), category/altitude filter chips, tap-on-blip selection.
class PageAircraft : public Page {
public:
  PageAircraft(AircraftProvider& ap, LocationService& loc, Settings& settings)
    : _ap(ap), _loc(loc), _settings(settings) {}

  const char* title() const override { return "Aircraft"; }
  void onEnter(App& app) override { _dirty = _needClear = true; }
  void onData(App& app, ProviderId id) override;
  void onTouch(App& app, int x, int y) override;
  void tick(App& app, uint32_t nowMs) override;

private:
  void draw(App& app);
  void drawMessage(App& app, const char* msg);
  void drawRadiusBadge(App& app);
  bool handleRadiusTap(App& app, int x, int yRel);

  AircraftProvider& _ap;
  LocationService&  _loc;
  Settings&         _settings;
  int   _sel = -1;
  bool  _dirty = true;
  bool  _needClear = true;   // full-clear only on structural change (anti-flicker)
  bool  _wasEmpty = true;    // track empty<->populated to clear only on transition
  uint32_t _lastDraw = 0;
};
