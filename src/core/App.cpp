#include "App.h"
#include "Page.h"
#include "ClockOverlay.h"
#include "EventBus.h"
#include "Scheduler.h"
#include "Theme.h"
#include "../hal/Display.h"
#include "../hal/Touch.h"
#include "../services/Settings.h"
#include "../services/LocationService.h"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <string.h>
#include <math.h>

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

bool App::pageAutoSkip(int idx) const {
  return idx >= 0 && idx < (int)_pages.size() && _pages[idx]->autoSkip();
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

void App::setAlert(const String& s, int targetPage) {
  _alertTarget = s.length() ? targetPage : -1;
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

void App::repaintActive() {                                  // clean full repaint (clock-mode exit)
  _display.gfx().fillRect(0, contentY(), contentW(), contentH(), gTheme.bg);
  if (_active >= 0) _pages[_active]->onEnter(*this);
  _statusDirty = true;
}

// Shared horizontal chip row (ADS-B + METAR field selectors). chip j -> labels[j].
int App::drawChipRow(int x0, int top, int h, const String* labels, int n, int sel,
                     int* hitX, int* hitW, int maxN) {
  auto& g = _display.gfx();
  g.setTextSize(1);
  g.setTextDatum(textdatum_t::middle_left);
  const int cw = contentW();
  int x = x0, cnt = 0;
  for (int i = 0; i < n && cnt < maxN; ++i) {
    int w = (int)labels[i].length() * 6 + 8;
    if (x + w > cw - 2) break;
    bool s = (i == sel);
    g.fillRect(x, top, w, h, s ? gTheme.accent : gTheme.grid);
    g.setTextColor(s ? gTheme.bg : gTheme.fg, s ? gTheme.accent : gTheme.grid);
    g.drawString(labels[i], x + 4, top + h / 2);
    hitX[cnt] = x; hitW[cnt] = w; cnt++;
    x += w + 3;
  }
  return cnt;
}

// Vertical "view-position" dots on the right edge (mirrors the top-bar page dots):
// where you are in a page's sub-view structure. Pages call this from draw().
void App::drawViewDots(int count, int index) {
  if (count < 2) return;
  auto& g = _display.gfx();
  const int gap = 8, x = contentW() - 5;
  int y0 = contentY() + (contentH() - (count - 1) * gap) / 2;
  for (int i = 0; i < count; ++i) {
    if (i == index) g.fillCircle(x, y0 + i * gap, 2, gTheme.accent);
    else            g.drawCircle(x, y0 + i * gap, 2, gTheme.dim);
  }
}

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
    String st = _pages[i]->gridStatus();             // live token surfaced from the page
    const int cx = x + cwc / 2, maxChars = (cwc - 6) / 6 < 4 ? 4 : (cwc - 6) / 6;
    if (!st.length()) { g.drawString(t.substring(0, maxChars), cx, yy + chc / 2); continue; }
    g.drawString(t.substring(0, maxChars), cx, yy + 16);
    g.setTextColor(gTheme.dim, gTheme.bg);           // word-wrap the status into the tile
    int ly = yy + 30, maxLines = (chc - 30) / 9; if (maxLines > 3) maxLines = 3;
    int start = 0, line = 0; String cur;
    auto flush = [&]() { if (cur.length() && line < maxLines) { g.drawString(cur, cx, ly); ly += 9; line++; } cur = ""; };
    while (start <= (int)st.length() && line < maxLines) {
      int sp = st.indexOf(' ', start), nlp = st.indexOf('\n', start);   // '\n' = forced line break
      bool nlFirst = (nlp >= 0 && (sp < 0 || nlp < sp));
      int end = nlFirst ? nlp : sp;
      String word = (end < 0) ? st.substring(start) : st.substring(start, end);
      String trial = cur.length() ? cur + " " + word : word;
      if ((int)trial.length() <= maxChars) cur = trial;
      else { flush(); cur = word.substring(0, maxChars); }
      if (nlFirst) flush();
      if (end < 0) break; start = end + 1;
    }
    flush();
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
  if (_locPicker) {                                          // location picker open: pick a row or dismiss
    if (y >= contentY()) {
      int r = locPickerRow(x, y - contentY());
      if (r >= 0) { applyLocation(r); closeLocPicker(); }
      else closeLocPicker();
    } else closeLocPicker();
    return;
  }
  if (_viewMenu) {                                           // views menu open: jump to a view or dismiss
    int r = (y >= contentY()) ? viewMenuRow(y - contentY()) : -1;
    if (r >= 0 && _active >= 0) {
      Page* p = _pages[_active];
      int cur = p->viewIndex(), n = p->viewCount();
      int steps = ((r - cur) % n + n) % n;                   // step forward (cycleView skips hidden views)
      for (int s = 0; s < steps; ++s) p->cycleView(1);
    }
    closeViewMenu();
    return;
  }
  if (_grid) {                                               // grid open: pick a cell or dismiss
    if (y >= contentY()) {
      int idx = gridCell(x, y - contentY());
      if (idx >= 0) {
        bool badged = (idx < (int)_badge.size() && _badge[idx]);
        _grid = false; _mode = Mode::Manual; setPage(idx);
        if (badged) { _badge[idx] = false; _pages[idx]->focusAlert(*this); }  // land on the alert (e.g. SPECI)
      }
      else closeGrid();
    } else closeGrid();
    return;
  }
  if (_clock && _clock->active() && y >= contentY()) {       // clock mode: chips toggle, else exit
    if (!_clock->handleTap(*this, x, y - contentY())) _clock->toggle(*this);
    return;
  }
  if (y < contentY()) {                                      // status strip
    if (x < 48 && _clock) { _clock->toggle(*this); return; } // tap the clock -> clock mode on/off
    if (_alert.length() && _alertTarget >= 0 && x < _display.width() - 49) {  // tap the alert text ->
      _mode = Mode::Manual; setPage(_alertTarget); _statusDirty = true; return;  // its page (glyphs stay)
    }
    if (dotsHit(x)) { openGrid(); return; }                  // tap the page dots -> grid
    const int W = _display.width();
    if (x >= W - 15) {                                       // WiFi bars -> Device Health
      int h = healthPageIndex();
      if (h >= 0) { _mode = Mode::Manual; setPage(h); _statusDirty = true; return; }
    }
    if (x >= W - 46 && x < W - 30 && openLocPicker()) return; // crosshair -> saved-locations picker
    if (titleHit(x) && openViewMenu()) return;               // page title -> views menu
    if (_pinned) _pinned = false;                            // (mode-glyph zone + rest) -> toggle mode
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
  if (_injScroll) {
    int dy = _injScroll; _injScroll = 0; _lastInteractMs = nowMs; _mode = Mode::Manual;
    if (_active >= 0 && !_grid) _pages[_active]->onScroll(*this, dy);
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
    if (_locPicker || _viewMenu) { tapAt(_pressX, _pressY); } // modal up: any release picks/dismisses
    else if (abs(dx) >= kSwipeMin && abs(dx) > abs(dy)) {   // horizontal swipe -> page nav
      if (_grid) closeGrid(); else { if (dx < 0) nextPage(); else prevPage(); }
    } else if (abs(dy) >= kSwipeMin && abs(dy) >= abs(dx)) {  // vertical swipe -> page scroll
      if (_grid) closeGrid();
      else if (_active >= 0 && _pressY >= contentY()) _pages[_active]->onScroll(*this, dy);
    } else if (!moved) {
      if (!_grid && _pressY >= contentY() && held > 700) { // long-press (stationary) = pin (§7.4)
        _pinned = !_pinned; _statusDirty = true;
      } else {
        tapAt(_pressX, _pressY);                            // grid / dots / status / page
      }
    }
    _wasTouched = false;
  }

  // Inactivity: MANUAL -> AUTO (unless pinned or an overlay is up) (spec §7.4).
  if (_mode == Mode::Manual && !_pinned && !_grid && !_locPicker && !_viewMenu && nowMs - _lastInteractMs > _inactivityMs) {
    _mode = Mode::Auto; _statusDirty = true;
  }
  // Clock screensaver: when idle in AUTO and the "auto" chip is on, raise the clock.
  if (_mode == Mode::Auto && _clock && _clock->autoUp() && !_clock->active()
      && !_grid && !_locPicker && nowMs - _lastInteractMs > _inactivityMs) {
    _clock->toggle(*this);
  }

  if (_active >= 0 && !_grid && !_locPicker && !_viewMenu) {   // grid/picker/views overlay holds the content
    if (_clock && _clock->active()) {
      bool live = _pages[_active]->clockKeepLive();            // live pages keep running underneath
      if (_active != _clockShownPage) { _clock->invalidate(); _clockShownPage = _active; }
      _clock->prepare(*this, nowMs, live);                     // pick corner; repaint page on a hop
      _pages[_active]->tick(*this, nowMs);                     // page draws live underneath
      _clock->stamp(*this);                                    // clock on top in its corner
    } else {
      _clockShownPage = -1;                                   // next clock-on starts with a clean redraw
      _pages[_active]->tick(*this, nowMs);
      drawViewDots(_pages[_active]->viewCount(), _pages[_active]->viewIndex());  // right-edge view position
    }
  }

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

  // Right cluster (left->right): page title, location crosshair, mode glyph, WiFi bars.
  // Spaced so the three glyphs read as distinct tap targets.
  const int cy = kStatusH / 2, barsRight = _display.width() - 4;
  drawSignalBars(barsRight, cy);                  // tap -> Device Health
  const int modeRight = barsRight - 17;
  drawModeIcon(modeRight, cy);                     // AUTO / MAN / PIN
  const int locCx = modeRight - 17;
  if (_pickSettings) drawLocIcon(locCx, cy);       // tap -> saved-locations picker
  if (_alert.length()) {                            // alert REPLACES the title (warn) but leaves the
    g.setTextDatum(textdatum_t::middle_right);       // clock + right glyphs visible/tappable (not full-strip)
    g.setTextColor(gTheme.warn, gTheme.grid);
    g.drawString(String("\xC2 ") + _alert, locCx - 11, cy);
  } else if (_active >= 0) {
    g.setTextDatum(textdatum_t::middle_right);
    g.setTextColor(gTheme.fg, gTheme.grid);
    g.drawString(_pages[_active]->title(), locCx - 11, cy);
  }

  // Page-indicator dots just right of the clock — hidden while an alert occupies that
  // space. A badged page (suppressed interrupt) shows a warn-coloured dot (spec §7.4).
  int n = (int)_pages.size();
  if (n > 1 && !_alert.length()) {
    int gap = 8, x0 = 52, cy = kStatusH / 2;
    for (int i = 0; i < n; ++i) {
      bool badged = (i < (int)_badge.size() && _badge[i]);
      if (i == _active)   g.fillCircle(x0 + i * gap, cy, 2, gTheme.accent);
      else if (badged)    g.fillCircle(x0 + i * gap, cy, 2, gTheme.warn);
      else                g.drawCircle(x0 + i * gap, cy, 2, gTheme.dim);
    }
  }
}

// WiFi signal-strength glyph: four bars filled by RSSI; a disconnected radio shows
// an empty outline with a warn slash. Right edge of the bars is at xRight.
void App::drawSignalBars(int xRight, int cy) {
  auto& g = _display.gfx();
  bool up = (WiFi.status() == WL_CONNECTED);
  int rssi = up ? WiFi.RSSI() : -127;
  int lvl = !up ? 0 : rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;   // 0..4 bars
  const int bw = 2, gap = 1, nb = 4;
  int x0 = xRight - (nb * (bw + gap) - gap);   // leftmost bar
  int baseY = cy + 6;                          // shared bar bottom
  for (int i = 0; i < nb; ++i) {
    int h = 3 + i * 3, bx = x0 + i * (bw + gap), by = baseY - h;
    if (i < lvl) g.fillRect(bx, by, bw, h, gTheme.fg);
    else         g.drawRect(bx, by, bw, h, gTheme.dim);
  }
  if (!up) g.drawLine(x0, baseY - 12, xRight, baseY, gTheme.warn);   // disconnected: slash
}

int App::healthPageIndex() const {
  for (int i = 0; i < (int)_pages.size(); ++i)
    if (!strcmp(_pages[i]->title(), "Device Health")) return i;
  return -1;
}

// Mode glyph: AUTO = play (touring), MAN = pause (stopped on a page), PIN = padlock.
void App::drawModeIcon(int xr, int cy) {
  auto& g = _display.gfx();
  if (_pinned) {                                  // padlock
    g.drawRect(xr - 5, cy - 5, 4, 3, gTheme.warn); // shackle
    g.fillRect(xr - 6, cy - 2, 6, 6, gTheme.warn); // body
  } else if (_mode == Mode::Auto) {               // play triangle
    g.fillTriangle(xr - 7, cy - 4, xr - 7, cy + 4, xr, cy, gTheme.ok);
  } else {                                        // pause bars
    g.fillRect(xr - 6, cy - 4, 2, 8, gTheme.fg);
    g.fillRect(xr - 2, cy - 4, 2, 8, gTheme.fg);
  }
}

// Location crosshair (GPS-style): a ring with four ticks. Opens the saved-locations picker.
void App::drawLocIcon(int cx, int cy) {
  auto& g = _display.gfx();
  Color c = gTheme.accent;
  g.drawCircle(cx, cy, 3, c);
  g.drawFastVLine(cx, cy - 5, 2, c); g.drawFastVLine(cx, cy + 4, 2, c);
  g.drawFastHLine(cx - 5, cy, 2, c); g.drawFastHLine(cx + 4, cy, 2, c);
}

bool App::openLocPicker() {
  if (!_pickSettings) return false;
  JsonArray locs = _pickSettings->doc()["locations"].as<JsonArray>();
  if (locs.isNull() || locs.size() == 0) return false;   // nothing saved -> configure in the web UI
  _locPicker = true; _mode = Mode::Manual; _statusDirty = true;
  drawLocPicker();
  return true;
}

void App::closeLocPicker() {
  if (!_locPicker) return;
  _locPicker = false;
  _display.gfx().fillRect(0, contentY(), contentW(), contentH(), gTheme.bg);
  if (_active >= 0) _pages[_active]->onEnter(*this);
  _statusDirty = true;
}

static const int kPickRowH = 19, kPickTop = 32;   // picker row geometry (shared by draw + hit-test)

void App::drawLocPicker() {
  auto& g = _display.gfx();
  const int cw = contentW(), ch = contentH(), y0 = contentY();
  g.fillRect(0, y0, cw, ch, gTheme.bg);
  g.setTextSize(1);
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString("Choose location  (tap one - or outside to cancel)", 6, y0 + 4);

  bool isAuto = _pickSettings->getString("locMode", "auto") != "preset";
  String activeName = _pickSettings->getString("locName", "");
  double aLat = _pickSettings->getFloat("locLat", 0), aLon = _pickSettings->getFloat("locLon", 0);
  // Current selection, so it's clear what's active even if it isn't a saved entry.
  g.setTextColor(gTheme.fg, gTheme.bg);
  char curln[48];
  if (isAuto) snprintf(curln, sizeof(curln), "current: Auto (IP)");
  else        snprintf(curln, sizeof(curln), "current: %s  %.2f, %.2f", activeName.c_str(), aLat, aLon);
  g.drawString(curln, 6, y0 + 16);

  JsonArray locs = _pickSettings->doc()["locations"].as<JsonArray>();
  g.setTextDatum(textdatum_t::middle_left);
  int ry = y0 + kPickTop;
  auto row = [&](const String& label, const String& sub, bool active) {
    if (ry + kPickRowH > y0 + ch) return;
    if (active) g.fillRect(4, ry, cw - 8, kPickRowH - 2, gTheme.grid);
    g.setTextColor(active ? gTheme.accent : gTheme.fg, active ? gTheme.grid : gTheme.bg);
    g.drawString(active ? ("\x10 " + label) : label, 10, ry + kPickRowH / 2 - 1);   // arrow marks the active row
    if (sub.length()) {
      g.setTextDatum(textdatum_t::middle_right);
      g.setTextColor(gTheme.dim, active ? gTheme.grid : gTheme.bg);
      g.drawString(sub, cw - 10, ry + kPickRowH / 2 - 1);
      g.setTextDatum(textdatum_t::middle_left);
    }
    ry += kPickRowH;
  };
  row("Auto (IP geolocation)", "", isAuto);                        // row 0
  for (JsonObject p : locs) {                                      // rows 1..n
    double lat = p["lat"] | 0.0, lon = p["lon"] | 0.0;
    char sub[24]; snprintf(sub, sizeof(sub), "%.2f, %.2f", lat, lon);
    String nm = (const char*)(p["name"] | "(unnamed)");
    bool active = !isAuto && (nm == activeName ||                  // match by name or by coordinates
                              (fabs(lat - aLat) < 0.01 && fabs(lon - aLon) < 0.01));
    row(nm, sub, active);
  }
}

int App::locPickerRow(int x, int yRel) const {
  if (yRel < kPickTop) return -1;
  int r = (yRel - kPickTop) / kPickRowH;
  int n = 1 + (int)_pickSettings->doc()["locations"].as<JsonArray>().size();
  return (r >= 0 && r < n) ? r : -1;
}

// Tap the page title (status strip) -> a modal list of the active page's views; tap one
// to jump straight to it (vs. centre-tap/swipe stepping). Only opens when >1 view.
bool App::titleHit(int x) const {
  if (_active < 0) return false;
  int tw = (int)strlen(_pages[_active]->title()) * 6;
  int right = _display.width() - 49;            // title right edge (left of crosshair/mode/bars)
  return x <= right + 2 && x >= right - tw - 2;
}

bool App::openViewMenu() {
  if (_active < 0 || _pages[_active]->viewCount() <= 1) return false;
  _viewMenu = true; _mode = Mode::Manual; _statusDirty = true;
  drawViewMenu();
  return true;
}

void App::closeViewMenu() {
  if (!_viewMenu) return;
  _viewMenu = false;
  _display.gfx().fillRect(0, contentY(), contentW(), contentH(), gTheme.bg);
  if (_active >= 0) _pages[_active]->onEnter(*this);
  _statusDirty = true;
}

void App::drawViewMenu() {
  auto& g = _display.gfx();
  const int cw = contentW(), ch = contentH(), y0 = contentY();
  Page* p = _pages[_active];
  g.fillRect(0, y0, cw, ch, gTheme.bg);
  g.setTextSize(1);
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString(String(p->title()) + " views  (tap one - or outside to cancel)", 6, y0 + 4);
  int n = p->viewCount(), cur = p->viewIndex(), ry = y0 + kPickTop;
  g.setTextDatum(textdatum_t::middle_left);
  for (int i = 0; i < n && ry + kPickRowH <= y0 + ch; ++i) {
    bool active = (i == cur);
    if (active) g.fillRect(4, ry, cw - 8, kPickRowH - 2, gTheme.grid);
    g.setTextColor(active ? gTheme.accent : gTheme.fg, active ? gTheme.grid : gTheme.bg);
    const char* nm = p->viewName(i);
    String label = nm ? String(nm) : ("View " + String(i + 1));
    g.drawString(active ? ("\x10 " + label) : label, 10, ry + kPickRowH / 2 - 1);
    ry += kPickRowH;
  }
}

int App::viewMenuRow(int yRel) const {
  if (_active < 0 || yRel < kPickTop) return -1;
  int r = (yRel - kPickTop) / kPickRowH;
  return (r >= 0 && r < _pages[_active]->viewCount()) ? r : -1;
}

void App::applyLocation(int row) {
  if (!_pickSettings) return;
  if (row == 0) {
    _pickSettings->set("locMode", "auto");
  } else {
    JsonArray locs = _pickSettings->doc()["locations"].as<JsonArray>();
    if (row - 1 >= (int)locs.size()) return;
    JsonObject p = locs[row - 1];
    _pickSettings->set("locMode", "preset");
    _pickSettings->set("locName", (const char*)(p["name"] | "Preset"));
    _pickSettings->set("locLat", (double)(p["lat"] | 0.0));
    _pickSettings->set("locLon", (double)(p["lon"] | 0.0));
  }
  _pickSettings->save();
  if (_pickLoc) _pickLoc->refresh();          // re-resolve + publish Location to all providers
}
