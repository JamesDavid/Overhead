#pragma once
#include "../core/Page.h"
#include <Arduino.h>

class TimeService;
class LocationService;

// pages/PageStarMap — all-sky star chart (spec §6, stretch). Azimuthal projection
// (zenith centre, horizon edge), stars sized by magnitude, major constellation
// lines, with a magnitude-limit filter (bottom-left badge) and a labels toggle
// (centre tap). A "tour" mode (bottom-centre badge) smoothly zooms into each
// above-horizon constellation in turn, naming its stars, then zooms back out.
// Prototype catalog in assets/StarCatalog.h.
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
  void updateTour(App& app, uint32_t nowMs);                 // advance the zoom animation
  bool conFocus(App& app, int con, int& fx, int& fy, int& count);  // centroid + visible count
  int  nextVisibleCon(App& app, int from);                   // next con with >=3 stars up
  bool starInCon(int con, const char* name) const;

  TimeService&     _time;
  LocationService& _loc;
  float _magLimit = 3.0f;
  bool  _labels = true;
  bool  _showSS = true;          // overlay Sun/Moon/planets + ecliptic
  bool  _dirty = true;
  uint32_t _lastDraw = 0;

  // Constellation zoom tour.
  bool     _tour = false;
  int      _tourCon = -1;        // constellation being framed (-1 = pick on next tick)
  uint8_t  _phase = 0;           // 0 zoom-in, 1 hold, 2 zoom-out
  uint32_t _phaseMs = 0;
  float    _t = 0.0f;            // 0 = full sky, 1 = fully zoomed on _tourCon
};
