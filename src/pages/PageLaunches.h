#pragma once
#include "../core/Page.h"
#include <Arduino.h>

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
  void onEnter(App& app) override { _dirty = true; }
  void onData(App& app, ProviderId id) override;
  void onTouch(App& app, int x, int y) override;
  void tick(App& app, uint32_t nowMs) override;

private:
  void draw(App& app);
  void drawMessage(App& app, const char* msg);

  LaunchProvider& _lp;
  TimeService&    _time;
  int   _sel = 0;
  bool  _dirty = true;
  uint32_t _lastDraw = 0;
};
