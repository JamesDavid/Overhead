#pragma once
#include <stdint.h>
#include <vector>
#include <Arduino.h>

class Display;
class Touch;
class EventBus;
class Scheduler;
class Page;
class ClockOverlay;
class Settings;
class LocationService;
class MorseBeeper;

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
  bool pageAutoSkip(int idx) const;      // page wants to be skipped in the AUTO rotation now
  // One-shot focus hint handed to the next page on entry (e.g. Agenda tap -> the
  // exact satellite/launch). The target page reads it in onEnter via takeFocus().
  void   requestFocus(const String& ref) { _pendingFocus = ref; }
  String takeFocus() { String f = _pendingFocus; _pendingFocus = ""; return f; }
  bool autoFocus(int index);            // switch only if AUTO & unpinned; true if switched
  bool autoAdvanceActive();             // step active page's tour; true if it completed a full cycle
  void setBadge(int index, bool on);
  void setAlert(const String& s, int targetPage = -1);  // cross-tab alert; tap the strip -> targetPage
  void setBeeper(MorseBeeper* b) { _beeper = b; }       // Morse-code alert beeper (optional)
  void beepTest(const String& w);                       // force a Morse beep (remote/serial test)
  void injectTap(int x, int y) { _injTapX = x; _injTapY = y; }   // debug: synthetic touch
  void injectSwipe(int dir) { _injSwipe = dir; }                 // debug: -1 prev, +1 next
  void injectScroll(int dy) { _injScroll = dy; }                 // debug: vertical scroll (dy<0 up, dy>0 down)
  void setInactivityMs(uint32_t ms) { _inactivityMs = ms; }
  uint32_t idleMs(uint32_t now) const { return now - _lastInteractMs; }

  Display& display() { return _display; }

  // Content rect available to pages (below the status strip).
  int contentY() const { return statusH(); }
  int statusH()  const;                  // status-strip height, scaled: kStatusH * ui()
  int contentH() const;
  int contentW() const;

  // UI scale tier for resolution-relative layout: 1 on a CYD-class screen (<640 px wide), 2 on the
  // 800x480 CrowPanel. Pages multiply layout literals (text size, row step, margins) by ui() so they
  // render natively/crisply at each size (ui()==1 leaves the CYD layout byte-for-byte unchanged).
  int ui() const;

  void openGrid();                       // show the 3x3 quick-jump grid (page taps open it)
  void setPin(bool on) { _pinned = on; _statusDirty = true; }   // pages can pin (clock rests)

  void setClockOverlay(ClockOverlay* c) { _clock = c; }         // device-wide bouncing-clock screensaver
  // Wire the saved-locations picker (status-strip crosshair icon -> modal list).
  void setLocationPicker(Settings* s, LocationService* loc) { _pickSettings = s; _pickLoc = loc; }
  void repaintActive();                  // force a clean full repaint of the active page
  void drawViewDots(int count, int index);   // vertical view-position dots on the right edge
  // Shared horizontal chip row (ADS-B field chips, METAR field chips...). Draws
  // `labels` highlighting index `sel`, writes per-chip hit boxes into hitX/hitW
  // (sized >= maxN); returns the number drawn (chip j maps to labels[j]).
  int  drawChipRow(int x0, int top, int h, const String* labels, int n, int sel,
                   int* hitX, int* hitW, int maxN);

private:
  void drawStatus();
  void drawGrid();                       // render the 3x3 page grid over the content area
  void closeGrid();                      // dismiss the grid + repaint the active page
  void tapAt(int x, int y);              // route a tap (grid / dots / status / page)
  int  gridCell(int x, int yRel) const;  // page index under a grid tap, or -1
  bool dotsHit(int x) const;             // x falls on the status-strip page dots
  void drawSignalBars(int xRight, int cy, int s);  // WiFi signal-strength bars glyph (s = UI scale)
  void drawModeIcon(int xRight, int cy, int s);    // AUTO(play) / MAN(pause) / PIN(lock) glyph
  void drawLocIcon(int cx, int cy, int s);         // location crosshair glyph (saved-locations picker)
  int  healthPageIndex() const;          // index of the Device Health page (-1 if absent)
  bool openLocPicker();                  // saved-locations modal: true if it opened (has entries)
  void closeLocPicker();
  void drawLocPicker();
  int  locPickerRow(int x, int yRel) const; // picker row under a tap, or -1
  void applyLocation(int row);           // switch the active location (row 0 = Auto/IP)
  bool openViewMenu();                   // tap the page title: list the active page's views; true if opened
  void closeViewMenu();
  void drawViewMenu();
  int  viewMenuRow(int yRel) const;      // view-menu row under a tap, or -1
  bool titleHit(int x) const;            // x falls on the status-strip page title

  Display&   _display;
  Touch&     _touch;
  EventBus&  _bus;
  Scheduler& _sched;

  std::vector<Page*> _pages;
  std::vector<bool>  _badge;
  int  _active = -1;
  String _pendingFocus;                // one-shot focus hint for the next page's onEnter
  ClockOverlay* _clock = nullptr;      // device-wide clock-mode screensaver (null until wired)
  int  _clockShownPage = -1;           // page the clock last composed over (detect Director switches)

  bool     _wasTouched   = false;
  int      _pressX = 0, _pressY = 0;   // touch-down point
  int      _lastX  = 0, _lastY  = 0;   // last point while touched
  uint32_t _lastStatusMs = 0;
  bool     _statusDirty  = true;
  MorseBeeper* _beeper = nullptr;  // optional: beep the alert call sign in Morse
  String   _alert;               // Director alert text (shown in the status strip)
  int      _alertTarget = -1;    // page to jump to when the alert banner is tapped (-1 none)
  String   _switchBanner;        // brief "switched to X" banner on an auto-switch
  uint32_t _switchBannerMs = 0;
  volatile int _injTapX = -1, _injTapY = -1;   // pending injected touch (debug web API)
  volatile int _injSwipe = 0;                  // pending injected swipe (-1/+1)
  volatile int _injScroll = 0;                 // pending injected vertical scroll (dy)

  bool     _grid = false;              // 3x3 quick-jump grid overlay is showing
  bool     _locPicker = false;         // saved-locations modal overlay is showing
  bool     _viewMenu = false;          // page-title views menu overlay is showing
  Settings*        _pickSettings = nullptr;   // for the location picker (saved `locations`)
  LocationService* _pickLoc = nullptr;
  Mode     _mode = Mode::Auto;
  bool     _pinned = false;
  uint32_t _lastInteractMs = 0;
  uint32_t _pressStartMs = 0;
  uint32_t _lastTouchMs = 0;     // for release debounce (resistive touch flickers)
  uint32_t _inactivityMs = 90000;

  static constexpr int kStatusH   = 20;
  static constexpr int kSwipeMin  = 28;   // px to count as a swipe
  static constexpr int kTapMax    = 18;   // px movement still a tap
};
