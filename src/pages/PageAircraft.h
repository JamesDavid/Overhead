#pragma once
#include "../core/Page.h"
#include <Arduino.h>

class AircraftProvider;
class AviationWxProvider;
class LocationService;
class Settings;

// pages/PageAircraft — the Aircraft tab (spec §6). Observer-centred radar (N-up,
// range rings), heading chevrons, and a selected-contact detail panel. Tap
// left/right to step through contacts (sorted by range). loading/empty/error.
// Deferred (backlog): nearest-airport + likely-frequency (needs bundled
// OurAirports subset), category/altitude filter chips, tap-on-blip selection.
class PageAircraft : public Page {
public:
  PageAircraft(AircraftProvider& ap, AviationWxProvider& wx, LocationService& loc, Settings& settings)
    : _ap(ap), _wx(wx), _loc(loc), _settings(settings) {}

  const char* title() const override { return "Aircraft"; }
  void onEnter(App& app) override;
  void onExit(App& app) override;
  void onData(App& app, ProviderId id) override;
  void onTouch(App& app, int x, int y) override;
  void tick(App& app, uint32_t nowMs) override;
  bool autoAdvance(App& app) override;

private:
  void draw(App& app);
  void drawMessage(App& app, const char* msg, int topY);
  void drawRadiusBadge(App& app);
  void drawGroundBadge(App& app);
  int  drawChips(App& app);                      // centre selector row; returns its height
  bool handleRadiusTap(App& app, int x, int yRel);
  bool handleGroundTap(App& app, int x, int yRel);
  bool handleChipTap(App& app, int x, int yRel);
  void applyCenter();                            // push _centerIcao to the provider + poll

  AircraftProvider&   _ap;
  AviationWxProvider& _wx;
  LocationService&    _loc;
  Settings&           _settings;
  int   _sel = -1;
  String _centerIcao;        // "" = observer (HOME); else recentre on this airport
  bool  _dirty = true;
  bool  _needClear = true;   // full-clear only on structural change (anti-flicker)
  bool  _wasEmpty = true;    // track empty<->populated to clear only on transition
  uint32_t _lastDraw = 0;

  static constexpr int kMaxChips = 8;
  int    _chipX[kMaxChips] = {0}, _chipW[kMaxChips] = {0}, _chipCount = 0;
  String _chipIcao[kMaxChips];

  int    _rCx = 0, _rCy = 0, _rR = 0;     // radar geometry (set in draw, for tap-on-blip)
  float  _rMaxR = 0;
};
