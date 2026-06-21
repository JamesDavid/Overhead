#pragma once
#include "../core/Page.h"
#include <Arduino.h>

class TimeService;
class LocationService;
class Settings;

// pages/PageStarMap — all-sky star chart (spec §6, stretch). Azimuthal projection
// (zenith centre, horizon edge), stars sized by magnitude, major constellation
// lines, with a magnitude-limit filter (bottom-left badge) and a labels toggle
// (centre tap). A "tour" mode (bottom-centre badge) smoothly zooms into each
// above-horizon constellation in turn, naming its stars, then zooms back out.
// Prototype catalog in assets/StarCatalog.h.
class PageStarMap : public Page {
public:
  PageStarMap(TimeService& time, LocationService& loc, Settings& settings)
    : _time(time), _loc(loc), _settings(settings) {}

  const char* title() const override { return "Star Map"; }
  void onEnter(App& app) override { _dirty = true; if (_view > memCount()) _view = 0; }
  void onData(App& app, ProviderId id) override { _dirty = true; }
  void onTouch(App& app, int x, int y) override;
  void tick(App& app, uint32_t nowMs) override;
  bool autoAdvance(App& app) override;
  String gridStatus() override;          // count of catalog stars above the horizon
  // Live "now / here" sky (view 0) plus one sub-view per saved Memory Sky.
  void cycleView(int dir) override;
  int  viewCount() const override { return 1 + memCount(); }
  int  viewIndex() const override { return _view; }
  const char* viewName(int i) const override;   // "Live sky" + memory-sky titles

private:
  void draw(App& app);
  void updateTour(App& app, uint32_t nowMs);                 // advance the zoom animation
  bool conFocus(App& app, int con, int& fx, int& fy, int& count);  // label centre + above-horizon flag
  int  nextVisibleCon(App& app, int from);                   // next con whose label is above the horizon
  int  memCount() const;                                     // number of saved Memory Skies
  void drawChart(App& app, double jd, double latDeg, double lonDeg);  // memory-sky natal chart readout
  // Effective sky for the active view: live now/here (view 0) or a saved sky.
  // false only when the live view has no time-sync / location yet.
  bool effective(double& jd, double& latDeg, double& lonDeg) const;

  TimeService&     _time;
  LocationService& _loc;
  Settings&        _settings;
  int   _view = 0;               // 0 = live now/here; 1..N = saved Memory Sky (index N-1)
  float _magLimit = 3.0f;
  bool  _labels = true;
  bool  _showSS = true;          // overlay Sun/Moon/planets + ecliptic
  bool  _chart = false;          // memory-sky natal-chart readout (tropical signs + Ascendant)
  bool  _dirty = true;
  uint32_t _lastDraw = 0;

  // Constellation zoom tour.
  bool     _tour = false;
  bool     _autoTour = false;    // Director-started tour (ends after one full pass)
  int      _tourCon = -1;        // constellation being framed (-1 = pick on next tick)
  int      _tourStart = -1;      // first con of the pass (detect a full loop)
  uint8_t  _phase = 0;           // 0 zoom-in, 1 hold, 2 zoom-out, 3 rest at full sky
  uint32_t _phaseMs = 0;
  float    _t = 0.0f;            // 0 = full sky, 1 = fully zoomed on _tourCon
  float    _drawnT = -2.0f;      // last-drawn t / con: skip redraw when unchanged
  int      _drawnCon = -2;       // (so the static hold phase doesn't flicker)

  // Tap-to-zoom: tap an area to zoom into it and label its stars; tap again to exit.
  bool     _zoom = false;
  float    _zoomT = 0.0f;        // 0 = wide, 1 = zoomed
  int      _zoomDir = 1;         // +1 zooming in, -1 zooming out
  int      _zFx = 0, _zFy = 0;   // focus = base screen coords of the tap
  uint32_t _zoomMs = 0;
};
