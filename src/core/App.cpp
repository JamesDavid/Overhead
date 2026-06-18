#include "App.h"
#include "Page.h"
#include "EventBus.h"
#include "Scheduler.h"
#include "Theme.h"
#include "../hal/Display.h"
#include "../hal/Touch.h"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <string.h>

// The one mutable global palette every widget reads.
Theme gTheme = themes::dark;

App::App(Display& display, Touch& touch, EventBus& bus, Scheduler& sched)
  : _display(display), _touch(touch), _bus(bus), _sched(sched) {}

int App::contentH() const { return _display.height() - kStatusH; }
int App::contentW() const { return _display.width(); }

void App::begin() {
  // Forward every provider update to the active page, and refresh the status
  // strip on time/location changes.
  _bus.subscribe([this](ProviderId id) {
    if (id == ProviderId::Time || id == ProviderId::Location) _statusDirty = true;
    if (_active >= 0) _pages[_active]->onData(*this, id);
  });

  _display.gfx().fillScreen(gTheme.bg);
  if (!_pages.empty() && _active < 0) setPage(0);
}

void App::addPage(Page* page) { _pages.push_back(page); _badge.push_back(false); }

int App::pageIndexByTitle(const char* title) const {
  for (size_t i = 0; i < _pages.size(); ++i)
    if (strcmp(_pages[i]->title(), title) == 0) return (int)i;
  return -1;
}

bool App::autoFocus(int index) {
  if (_mode != Mode::Auto || _pinned || index < 0 || index == _active) return false;
  setPage(index);
  if (index < (int)_badge.size()) _badge[index] = false;
  _switchBanner = _pages[index]->title();   // announce the auto-switch in the status strip
  _switchBannerMs = millis();
  _statusDirty = true;
  return true;
}

bool App::autoAdvanceActive() {
  if (_mode == Mode::Auto && !_pinned && _active >= 0) return _pages[_active]->autoAdvance(*this);
  return false;
}

void App::setAlert(const String& s) {
  if (_alert != s) { _alert = s; _statusDirty = true; }
}

void App::setBadge(int index, bool on) {
  if (index >= 0 && index < (int)_badge.size() && _badge[index] != on) {
    _badge[index] = on; _statusDirty = true;
  }
}

void App::setPage(int index) {
  if (index < 0 || index >= (int)_pages.size()) return;
  if (_active >= 0) _pages[_active]->onExit(*this);
  _active = index;
  _display.gfx().fillRect(0, contentY(), contentW(), contentH(), gTheme.bg);
  _pages[_active]->onEnter(*this);
  _statusDirty = true;
}

void App::nextPage() { if (!_pages.empty()) setPage((_active + 1) % _pages.size()); }
void App::prevPage() { if (!_pages.empty()) setPage((_active - 1 + _pages.size()) % _pages.size()); }

// --- 3x3 quick-jump grid overlay (first step of the desk-clock shell) -------------
void App::openGrid() {
  if (_pages.size() < 2) return;
  _grid = true; _mode = Mode::Manual; _statusDirty = true;   // user took over -> no auto-switch
  drawGrid();
}

void App::closeGrid() {
  if (!_grid) return;
  _grid = false;
  _display.gfx().fillRect(0, contentY(), contentW(), contentH(), gTheme.bg);
  if (_active >= 0) _pages[_active]->onEnter(*this);          // force a clean repaint
  _statusDirty = true;
}

void App::drawGrid() {
  auto& g = _display.gfx();
  const int cw = contentW(), ch = contentH(), y0 = contentY();
  g.fillRect(0, y0, cw, ch, gTheme.bg);
  const int cwc = cw / 3, chc = ch / 3, n = (int)_pages.size();
  g.setTextSize(1);
  g.setTextDatum(textdatum_t::middle_center);
  for (int i = 0; i < 9 && i < n; ++i) {
    int col = i % 3, row = i / 3, x = col * cwc, yy = y0 + row * chc;
    bool act = (i == _active);
    g.drawRect(x + 2, yy + 2, cwc - 4, chc - 4, act ? gTheme.accent : gTheme.grid);
    if (i < (int)_badge.size() && _badge[i]) g.fillCircle(x + cwc - 9, yy + 9, 2, gTheme.warn);
    g.setTextColor(act ? gTheme.accent : gTheme.fg, gTheme.bg);
    String t = _pages[i]->title();
    g.drawString(t.substring(0, (cwc - 8) / 6), x + cwc / 2, yy + chc / 2);
  }
}

int App::gridCell(int x, int yRel) const {
  const int cwc = contentW() / 3, chc = contentH() / 3;
  if (cwc <= 0 || chc <= 0) return -1;
  int col = x / cwc, row = yRel / chc;
  if (col < 0 || col > 2 || row < 0 || row > 2) return -1;
  int idx = row * 3 + col;
  return idx < (int)_pages.size() ? idx : -1;
}

bool App::dotsHit(int x) const {
  int n = (int)_pages.size();
  if (n <= 1) return false;
  const int x0 = 52, gap = 8;                                // mirrors drawStatus()
  return x >= x0 - 4 && x <= x0 + (n - 1) * gap + 4;
}

void App::tapAt(int x, int y) {
  if (_grid) {                                               // grid open: pick a cell or dismiss
    if (y >= contentY()) {
      int idx = gridCell(x, y - contentY());
      if (idx >= 0) { _grid = false; _mode = Mode::Manual; setPage(idx); }
      else closeGrid();
    } else closeGrid();
    return;
  }
  if (y < contentY()) {                                      // status strip
    if (dotsHit(x)) { openGrid(); return; }                  // tap the page dots -> grid
    if (_pinned) _pinned = false;
    else _mode = (_mode == Mode::Auto) ? Mode::Manual : Mode::Auto;
    _statusDirty = true;
    return;
  }
  _mode = Mode::Manual;                                      // content tap -> active page
  if (_active >= 0) _pages[_active]->onTouch(*this, x, y - contentY());
}

void App::tick(uint32_t nowMs) {
  _display.serviceShot();          // debug screenshot capture (UI thread, no SPI race)

  // Injected input from the debug web API (processed here on the UI thread).
  if (_injTapX >= 0) {
    int x = _injTapX, y = _injTapY; _injTapX = -1;
    _lastInteractMs = nowMs; _statusDirty = true;
    tapAt(x, y);
  }
  if (_injSwipe) {
    int d = _injSwipe; _injSwipe = 0; _lastInteractMs = nowMs; _mode = Mode::Manual;
    if (d < 0) prevPage(); else nextPage();
  }

  // Touch: distinguish a horizontal swipe (carousel page change) from a tap
  // (delivered to the active page). Hold/pin + the Director arrive later.
  int16_t tx, ty;
  bool touched = _touch.read(_display, tx, ty);
  if (touched) {
    if (!_wasTouched) {
      _pressX = tx; _pressY = ty; _pressStartMs = nowMs;
      if (ty >= contentY()) { _mode = Mode::Manual; _statusDirty = true; }  // content touch -> MANUAL
    }
    _lastX = tx; _lastY = ty; _lastInteractMs = nowMs; _lastTouchMs = nowMs;
    _wasTouched = true;
  } else if (_wasTouched && nowMs - _lastTouchMs >= 60) {  // debounced release (resistive flicker)
    int dx = _lastX - _pressX, dy = _lastY - _pressY;
    uint32_t held = _lastTouchMs - _pressStartMs;
    bool moved = abs(dx) > kTapMax || abs(dy) > kTapMax;
    if (abs(dx) >= kSwipeMin && abs(dx) > abs(dy)) {       // horizontal swipe -> page nav
      if (_grid) closeGrid(); else { if (dx < 0) nextPage(); else prevPage(); }
    } else if (!moved) {
      if (!_grid && _pressY >= contentY() && held > 700) { // long-press (stationary) = pin (§7.4)
        _pinned = !_pinned; _statusDirty = true;
      } else {
        tapAt(_pressX, _pressY);                            // grid / dots / status / page
      }
    }
    _wasTouched = false;
  }

  // Inactivity: MANUAL -> AUTO (unless pinned or the grid is up) (spec §7.4).
  if (_mode == Mode::Manual && !_pinned && !_grid && nowMs - _lastInteractMs > _inactivityMs) {
    _mode = Mode::Auto; _statusDirty = true;
  }

  if (_active >= 0 && !_grid) _pages[_active]->tick(*this, nowMs);   // grid overlay holds the content

  if (_switchBannerMs && nowMs - _switchBannerMs >= 4000) {   // banner expired -> restore strip
    _switchBannerMs = 0; _statusDirty = true;
  }

  if (_statusDirty || nowMs - _lastStatusMs >= 1000) {
    _lastStatusMs = nowMs;
    _statusDirty = false;
    drawStatus();
  }
}

void App::drawStatus() {
  auto& g = _display.gfx();

  // Clock (local; TimeService configures TZ so localtime is correct).
  time_t now = time(nullptr);
  char clk[16] = "--:--";
  if (now > 1600000000) {                 // only once NTP-synced (spec §13)
    struct tm tm; localtime_r(&now, &tm);
    strftime(clk, sizeof(clk), "%H:%M", &tm);
  }

  // Alert mode: paint the whole strip warn and show the Director's message — this
  // is the cross-tab "ISS pass NOW" notice, visible on any page (spec §7.4).
  if (_alert.length()) {
    g.fillRect(0, 0, _display.width(), kStatusH, gTheme.warn);
    g.setTextSize(1);
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextColor(gTheme.bg, gTheme.warn);
    g.drawString(clk, 6, kStatusH / 2);
    g.setTextDatum(textdatum_t::middle_right);
    g.drawString(String("\xC2 ") + _alert, _display.width() - 6, kStatusH / 2);
    return;
  }

  // Auto-switch banner: announce an ambient/Director page change for ~2.5s (the
  // interrupt case already shows _alert above; this covers the silent tour jumps).
  if (_switchBannerMs && millis() - _switchBannerMs < 4000) {
    g.fillRect(0, 0, _display.width(), kStatusH, gTheme.accent);
    g.setTextSize(1);
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextColor(gTheme.bg, gTheme.accent);
    g.drawString(clk, 6, kStatusH / 2);
    g.setTextDatum(textdatum_t::middle_right);
    g.drawString(String("\x10 ") + _switchBanner, _display.width() - 6, kStatusH / 2);
    return;
  }

  g.fillRect(0, 0, _display.width(), kStatusH, gTheme.grid);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(gTheme.fg, gTheme.grid);
  g.setTextSize(1);
  g.drawString(clk, 6, kStatusH / 2);

  // WiFi state + mode + active page title, right-aligned.
  g.setTextDatum(textdatum_t::middle_right);
  String wifi = (WiFi.status() == WL_CONNECTED) ? String("WiFi ") + WiFi.RSSI() : "WiFi --";
  const char* modeTag = _pinned ? "PIN" : (_mode == Mode::Auto ? "AUTO" : "MAN");
  String right = (_active >= 0 ? String(_pages[_active]->title()) + "  " : "")
               + modeTag + "  " + wifi;
  g.drawString(right, _display.width() - 6, kStatusH / 2);

  // Page-indicator dots just right of the clock. A badged page (Director has a
  // suppressed interrupt for it) shows a warn-coloured dot (spec §7.4).
  int n = (int)_pages.size();
  if (n > 1) {
    int gap = 8, x0 = 52, cy = kStatusH / 2;
    for (int i = 0; i < n; ++i) {
      bool badged = (i < (int)_badge.size() && _badge[i]);
      if (i == _active)   g.fillCircle(x0 + i * gap, cy, 2, gTheme.accent);
      else if (badged)    g.fillCircle(x0 + i * gap, cy, 2, gTheme.warn);
      else                g.drawCircle(x0 + i * gap, cy, 2, gTheme.dim);
    }
  }
}
