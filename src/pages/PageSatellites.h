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
  void onEnter(App& app) override;
  void onData(App& app, ProviderId id) override;
  void onTouch(App& app, int x, int y) override;
  void tick(App& app, uint32_t nowMs) override;

private:
  enum class View { Polar, Ground, Graph };
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
  void drawGraphView(App& app, const astro::SatObservation& o);
  void drawInfoColumn(App& app, int ix, int iy, const astro::SatObservation& o);
  void drawMinElBadge(App& app);

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
  std::vector<TrackPt> _track;
  std::vector<float>   _graphEl;       // elevation samples aos..los

  View     _view     = View::Polar;
  bool     _dirty    = true;
  bool     _needClear = true;       // full-clear only on structural change
  int16_t  _pdx = -1, _pdy = -1;    // last plotted blip (for erase, anti-flicker)
  uint32_t _lastDraw = 0;
};
