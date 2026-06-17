#pragma once
#include "../core/Page.h"
#include <Arduino.h>
#include <vector>

class LaunchProvider;
class TimeService;

// pages/PageLaunches — the Launches tab (spec §6). A next-launch card (provider,
// vehicle, mission, pad, status pill, big T-minus that respects net_precision)
// plus a short upcoming list. Tap left/right thirds to step through launches.
// loading/empty/error/stale states.
class PageLaunches : public Page {
public:
  PageLaunches(LaunchProvider& lp, TimeService& time) : _lp(lp), _time(time) {}

  const char* title() const override { return "Launches"; }
  void onEnter(App& app) override { _dirty = _needClear = true; }
  void onData(App& app, ProviderId id) override;
  void onTouch(App& app, int x, int y) override;
  void tick(App& app, uint32_t nowMs) override;
  bool autoAdvance(App& app) override;

private:
  void draw(App& app);
  void drawMessage(App& app, const char* msg);
  void drawBadges(App& app);     // bottom-left window + TBD filter chips
  void rebuildFilter();          // build _filtered (indices into _lp.launches())

  LaunchProvider& _lp;
  TimeService&    _time;
  int   _sel = 0;
  int   _window = 1;             // time window: 0=24h, 1=7d, 2=30d, 3=all
  bool  _hideTBD = false;        // drop NET-TBD launches
  std::vector<int> _filtered;    // launch indices passing the filter
  bool  _dirty = true;
  bool  _needClear = true;   // full-clear only on structural change (anti-flicker)
  uint32_t _lastDraw = 0;
};
