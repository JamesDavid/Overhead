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

void App::addPage(Page* page) { _pages.push_back(page); }

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

void App::tick(uint32_t nowMs) {
  // Touch: distinguish a horizontal swipe (carousel page change) from a tap
  // (delivered to the active page). Hold/pin + the Director arrive later.
  int16_t tx, ty;
  bool touched = _touch.read(_display, tx, ty);
  if (touched) {
    if (!_wasTouched) { _pressX = tx; _pressY = ty; }
    _lastX = tx; _lastY = ty;
  } else if (_wasTouched) {                 // release edge
    int dx = _lastX - _pressX, dy = _lastY - _pressY;
    if (abs(dx) >= kSwipeMin && abs(dx) > abs(dy)) {
      if (dx < 0) nextPage(); else prevPage();
    } else if (abs(dx) <= kTapMax && abs(dy) <= kTapMax &&
               _active >= 0 && _pressY >= contentY()) {
      _pages[_active]->onTouch(*this, _pressX, _pressY - contentY());
    }
  }
  _wasTouched = touched;

  if (_active >= 0) _pages[_active]->tick(*this, nowMs);

  if (_statusDirty || nowMs - _lastStatusMs >= 1000) {
    _lastStatusMs = nowMs;
    _statusDirty = false;
    drawStatus();
  }
}

void App::drawStatus() {
  auto& g = _display.gfx();
  g.fillRect(0, 0, _display.width(), kStatusH, gTheme.grid);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(gTheme.fg, gTheme.grid);
  g.setTextSize(1);

  // Clock (local; TimeService configures TZ so localtime is correct).
  time_t now = time(nullptr);
  char clk[16] = "--:--";
  if (now > 1600000000) {                 // only once NTP-synced (spec §13)
    struct tm tm; localtime_r(&now, &tm);
    strftime(clk, sizeof(clk), "%H:%M", &tm);
  }
  g.drawString(clk, 6, kStatusH / 2);

  // WiFi state + active page title, right-aligned.
  g.setTextDatum(textdatum_t::middle_right);
  String right;
  if (WiFi.status() == WL_CONNECTED) right = String("WiFi ") + WiFi.RSSI() + "dBm";
  else                               right = "WiFi --";
  if (_active >= 0) right = String(_pages[_active]->title()) + "  |  " + right;
  g.drawString(right, _display.width() - 6, kStatusH / 2);

  // Page-indicator dots (carousel position), just right of the clock so they
  // don't collide with the page title on the right.
  int n = (int)_pages.size();
  if (n > 1) {
    int gap = 8, x0 = 52, cy = kStatusH / 2;
    for (int i = 0; i < n; ++i) {
      if (i == _active) g.fillCircle(x0 + i * gap, cy, 2, gTheme.accent);
      else              g.drawCircle(x0 + i * gap, cy, 2, gTheme.dim);
    }
  }
}
