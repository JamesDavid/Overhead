#pragma once
#include "../core/Page.h"
#include <Arduino.h>

class TimeService;
class LocationService;
class Settings;

// pages/PageClock — a calm "desk clock" rest screen: big local time + date + a sun/moon
// line, over a selectable background (plain / starfield / orbit rings) cycled by the
// bottom chip. Reached by tapping the time in the status bar; auto-pins on enter so the
// Director leaves it be, and unpins on exit. Not in the ambient tour rotation.
class PageClock : public Page {
public:
  PageClock(TimeService& time, LocationService& loc, Settings& settings)
    : _time(time), _loc(loc), _settings(settings) {}

  const char* title() const override { return "Clock"; }
  void onEnter(App& app) override;
  void onExit(App& app) override;
  void onTouch(App& app, int x, int y) override;
  void tick(App& app, uint32_t nowMs) override;

private:
  void draw(App& app);
  void drawBackground(App& app);

  TimeService&     _time;
  LocationService& _loc;
  Settings&        _settings;
  int  _mode = 0;            // background: 0 plain, 1 starfield, 2 rings
  int  _lastMin = -1;
  bool _dirty = true;
  uint32_t _lastDraw = 0;
};
