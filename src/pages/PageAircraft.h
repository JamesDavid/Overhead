#pragma once
#include "../core/Page.h"
#include <Arduino.h>
#include <vector>

class AircraftProvider;
class AviationWxProvider;
class LocationService;
class Settings;
class AirportDB;

// pages/PageAircraft — the Aircraft tab (spec §6). Observer-centred radar (N-up,
// range rings), heading chevrons, and a selected-contact detail panel. Tap
// left/right to step through contacts (sorted by range). loading/empty/error.
// Deferred (backlog): nearest-airport + likely-frequency (needs bundled
// OurAirports subset), category/altitude filter chips, tap-on-blip selection.
class PageAircraft : public Page {
public:
  PageAircraft(AircraftProvider& ap, AviationWxProvider& wx, LocationService& loc, Settings& settings, AirportDB& adb)
    : _ap(ap), _wx(wx), _loc(loc), _settings(settings), _adb(adb) {}

  const char* title() const override { return "Aircraft"; }
  void onEnter(App& app) override;
  void onExit(App& app) override;
  void onData(App& app, ProviderId id) override;
  void onTouch(App& app, int x, int y) override;
  void tick(App& app, uint32_t nowMs) override;
  String gridStatus() override;
  bool autoAdvance(App& app) override;

private:
  void draw(App& app);
  void drawMessage(App& app, const char* msg, int topY);
  void drawRadiusBadge(App& app);
  void drawGroundBadge(App& app);
  int  drawChips(App& app);                      // centre selector row; returns its height
  bool handleRadiusTap(App& app, int x, int yRel);
  bool handleGroundTap(App& app, int x, int yRel);
  bool handleAltTap(App& app, int x, int yRel);  // altitude-band filter chip
  bool handleCatTap(App& app, int x, int yRel);  // category filter chip
  bool handleChipTap(App& app, int x, int yRel);
  void drawFilterBadges(App& app);               // alt + category filter chips (bottom)
  void drawAirportMarquee(App& app);             // scrolling nearest-field + freqs ticker
  void rebuildFilt();                            // _filt = contacts passing alt/cat filters
  void applyCenter();                            // push _centerIcao to the provider + poll

  AircraftProvider&   _ap;
  AviationWxProvider& _wx;
  LocationService&    _loc;
  Settings&           _settings;
  AirportDB&          _adb;
  int   _sel = -1;           // index into _filt (the filtered contact list)
  std::vector<int> _filt;    // indices into _ap.aircraft() passing the alt/cat filters
  int   _altF = 0;           // 0 all, 1 <10k, 2 10-25k, 3 >25k ft
  int   _catF = 0;           // 0 all, 1 airliner, 2 GA, 3 heli, 4 mil
  String _centerIcao;        // "" = observer (HOME); else recentre on this airport
  bool  _dirty = true;
  bool  _needClear = true;   // full-clear only on structural change (anti-flicker)
  bool  _wasEmpty = true;    // track empty<->populated to clear only on transition
  bool  _wasEmerg = false;   // track emergency-squawk presence (clear strip on change)
  uint32_t _lastDraw = 0;
  uint32_t _lastDataMs = 0;  // millis() of the last data update, for dead-reckoning blips
  uint32_t _marqMs = 0;      // last marquee scroll-frame time

  static constexpr int kMaxChips = 8;
  int    _chipX[kMaxChips] = {0}, _chipW[kMaxChips] = {0}, _chipCount = 0;
  String _chipIcao[kMaxChips];

  int    _rCx = 0, _rCy = 0, _rR = 0;     // radar geometry (set in draw, for tap-on-blip)
  float  _rMaxR = 0;
};
