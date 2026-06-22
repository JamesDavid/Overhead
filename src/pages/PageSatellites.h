#pragma once
#include "../core/Page.h"
#include "../astro/SatEngine.h"
#include <Arduino.h>
#include <vector>

class TleProvider;
class LocationService;
class TimeService;
class Settings;

// pages/PageSatellites — the Satellites tab (spec §6). Two views toggled by a
// centre tap: a polar az/el plot with an info panel, and an equirectangular
// world ground-track. Tap left/right thirds to step through the (watchlist-
// filtered) selection. Live Doppler for FM birds. Uses astro::SatEngine.
//
// Deferred (noted): az/el time graph, group/min-el filter chips, bundled map
// coastlines, grayline overlay (m6).
class PageSatellites : public Page {
public:
  PageSatellites(TleProvider& tle, LocationService& loc, TimeService& time, Settings& settings)
    : _tle(tle), _loc(loc), _time(time), _settings(settings) {}

  const char* title() const override { return "Satellites"; }
  bool clockKeepLive() const override { return true; }   // pass keeps counting under the clock
  void focusBird(const String& namePrefix);   // Director pre-focus (spec §7)
  void onEnter(App& app) override;
  void onData(App& app, ProviderId id) override;
  void onTouch(App& app, int x, int y) override;
  void cycleView(int dir) override;            // up/down swipe: Polar <-> Ground
  int  viewCount() const override { return 2; }
  int  viewIndex() const override { return _view == View::Ground ? 1 : 0; }
  const char* viewName(int i) const override { return i == 0 ? "Pass (polar)" : i == 1 ? "Ground track" : nullptr; }
  void tick(App& app, uint32_t nowMs) override;
  void invalidate() override { _dirty = true; }
  String gridStatus() override;
  bool autoAdvance(App& app) override;

private:
  enum class View { Polar, Ground };
  struct TrackPt { float lat; float lon; };

  void rebuildOrder();                 // filter provider sats by the watchlist
  void selectPos(int pos);
  void reloadSelected();
  void recomputePass(time_t now);
  void recomputeTrack(time_t now);
  void recomputeGraph();               // elevation samples across the pass
  int  minEl() const;
  bool handleMinElTap(App& app, int x, int yRel);

  void draw(App& app);
  void drawMessage(App& app, const char* msg);
  void drawPolarView(App& app, const astro::SatObservation& o);
  void drawGroundView(App& app, const astro::SatObservation& o);
  void drawInfoColumn(App& app, int ix, int iy, const astro::SatObservation& o);
  void drawMinElBadge(App& app);
  void drawChips(App& app);                    // tracked-sat selector chips (shared chip row)
  bool handleChipTap(App& app, int x, int yRel);

  static constexpr int kMaxChips = 12;
  int _chipX[kMaxChips] = {0}, _chipW[kMaxChips] = {0}, _chipN = 0, _chipBandH = 0;

  TleProvider&     _tle;
  LocationService& _loc;
  TimeService&     _time;
  Settings&        _settings;

  astro::SatEngine _eng;
  std::vector<int> _order;             // provider indices we navigate
  int   _orderPos = -1;
  int   _sel      = -1;                // absolute provider index (= _order[_orderPos])
  bool  _loaded   = false;
  astro::SatPass _pass;
  int   _passVis = 0;                   // naked-eye state: 0 n/a (radio bird) 1 visible 2 daylight 3 shadow
  std::vector<TrackPt> _track;
  std::vector<float>   _graphEl;       // elevation samples aos..los
  std::vector<float>   _passAz;        // matching azimuth samples (for the polar arc)

  View     _view     = View::Polar;
  bool     _dirty    = true;
  bool     _needClear = true;       // full-clear only on structural change
  int16_t  _pdx = -1, _pdy = -1;    // last plotted blip (for erase, anti-flicker)
  uint32_t _lastDraw = 0;
};
