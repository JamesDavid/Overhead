#pragma once
#include <stdint.h>
#include <vector>
#include <Arduino.h>

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

  // --- Attention lifecycle (spec §7.4), driven by the Director ---
  enum class Mode { Auto, Manual };
  Mode mode() const { return _mode; }
  bool pinned() const { return _pinned; }
  int  activeIndex() const { return _active; }
  int  pageIndexByTitle(const char* title) const;
  bool autoFocus(int index);            // switch only if AUTO & unpinned; true if switched
  bool autoAdvanceActive();             // step active page's tour; true if it completed a full cycle
  void setBadge(int index, bool on);
  void setAlert(const String& s);       // Director cross-tab alert in the status strip
  void setInactivityMs(uint32_t ms) { _inactivityMs = ms; }
  uint32_t idleMs(uint32_t now) const { return now - _lastInteractMs; }

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
  std::vector<bool>  _badge;
  int  _active = -1;

  bool     _wasTouched   = false;
  int      _pressX = 0, _pressY = 0;   // touch-down point
  int      _lastX  = 0, _lastY  = 0;   // last point while touched
  uint32_t _lastStatusMs = 0;
  bool     _statusDirty  = true;
  String   _alert;               // Director alert text (shown in the status strip)

  Mode     _mode = Mode::Auto;
  bool     _pinned = false;
  bool     _pinToggled = false;        // pin toggled during the current press
  uint32_t _lastInteractMs = 0;
  uint32_t _pressStartMs = 0;
  uint32_t _lastTouchMs = 0;     // for release debounce (resistive touch flickers)
  uint32_t _inactivityMs = 90000;

  static constexpr int kStatusH   = 20;
  static constexpr int kSwipeMin  = 28;   // px to count as a swipe
  static constexpr int kTapMax    = 18;   // px movement still a tap
};
