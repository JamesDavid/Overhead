#pragma once
#include <stdint.h>
#include <vector>

class Display;
class Touch;
class EventBus;
class Scheduler;
class Page;

// core/App — the app shell (spec §4). For milestone 1 this is intentionally
// minimal: it owns the page list, renders the always-visible status strip, and
// routes EventBus notifications + touch to the active page. The full chrome
// (swipe carousel, page-indicator, 3x3 quick-jump, corner-glyph overlays) lands
// with the first real tabs and the widget toolkit (milestone 3+).
class App {
public:
  App(Display& display, Touch& touch, EventBus& bus, Scheduler& sched);

  void begin();
  void addPage(Page* page);
  void setPage(int index);
  void nextPage();
  void prevPage();

  void tick(uint32_t nowMs);

  Display& display() { return _display; }

  // Content rect available to pages (below the status strip).
  int contentY() const { return kStatusH; }
  int contentH() const;
  int contentW() const;

  void requestStatusRedraw() { _statusDirty = true; }

private:
  void drawStatus();

  Display&   _display;
  Touch&     _touch;
  EventBus&  _bus;
  Scheduler& _sched;

  std::vector<Page*> _pages;
  int  _active = -1;

  bool     _wasTouched   = false;
  int      _pressX = 0, _pressY = 0;   // touch-down point
  int      _lastX  = 0, _lastY  = 0;   // last point while touched
  uint32_t _lastStatusMs = 0;
  bool     _statusDirty  = true;

  static constexpr int kStatusH   = 20;
  static constexpr int kSwipeMin  = 40;   // px to count as a swipe
  static constexpr int kTapMax    = 18;   // px movement still a tap
};
