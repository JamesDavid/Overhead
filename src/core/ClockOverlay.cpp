#include "ClockOverlay.h"
#include "App.h"
#include "Theme.h"
#include "../hal/Display.h"
#include "../services/TimeService.h"
#include "../services/Settings.h"
#include <Arduino.h>
#include <time.h>
#include <math.h>

static constexpr int      kStripH = 14;          // built-in chip strip at the bottom of the box
static constexpr uint16_t kKey    = 0xF81F;      // sprite transparent key (magenta; never used by the clock)

void ClockOverlay::begin() {
  _use24  = _settings.getBool("clk24", true);
  _ball   = _settings.getBool("clkBall", false);
  _scale  = (int)_settings.getInt("clkScale", 1); if (_scale < 0 || _scale > 2) _scale = 1;
  _autoUp = _settings.getBool("clkAuto", false);
}

void ClockOverlay::layoutBox(App& app) {
  static const float kSc[3] = {0.78f, 1.0f, 1.34f};   // small / medium / large
  float s = kSc[_scale < 0 || _scale > 2 ? 1 : _scale];
  if (_ball) { _bw = (int)(88 * s);  _gh = (int)(84 * s); }   // analog disc
  else       { _bw = (int)(128 * s); _gh = (int)(42 * s); }   // time + date
  _bh = _gh + 2 * kStripH;                  // two chip rows (format/style, auto/size)
  if (_bw > app.contentW()) _bw = app.contentW();
  if (_bh > app.contentH()) _bh = app.contentH();
}

void ClockOverlay::cornerPos(App& app, int corner, int& x, int& y) {
  const int m = 3, cw = app.contentW(), y0 = app.contentY(), ch = app.contentH();
  bool right  = (corner == 1 || corner == 3);
  bool bottom = (corner == 3);                  // (corner 2 / lower-left is avoided)
  x = right  ? cw - _bw - m : m;
  y = bottom ? y0 + ch - _bh - m : y0 + m;
}

int ClockOverlay::pickCorner() {
  static const int kCorners[] = {0, 1, 3};      // skip lower-left (chip row lives there)
  int c;
  do { _rng = _rng * 1664525u + 1013904223u; c = kCorners[(_rng >> 24) % 3]; } while (c == _corner);
  return c;
}

void ClockOverlay::toggle(App& app) {
  _on = !_on;
  if (_on) { begin(); _corner = 3; _firstFrame = true; _shownMin = -2; }
  else {
    if (_spr) { delete (lgfx::LGFX_Sprite*)_spr; _spr = nullptr; _sprW = _sprH = 0; }
    app.repaintActive();                        // clock gone -> restore the page cleanly
  }
}

void ClockOverlay::prepare(App& app, uint32_t now, bool live) {
  layoutBox(app);
  int next = _corner;
  if (live) next = 3;                            // live pages: parked lower-right
  else if (_firstFrame || now - _cornerMs >= 10000) { next = pickCorner(); _cornerMs = now; }

  bool hop = _firstFrame || (next != _corner);
  _corner = next; _firstFrame = false;
  if (hop) app.repaintActive();                  // clean full repaint; page redraws this same frame
}

void ClockOverlay::renderSprite(App& app) {
  auto& gfx = app.display().gfx();
  auto* spr = (lgfx::LGFX_Sprite*)_spr;
  if (spr && (_sprW != _bw || _sprH != _bh)) { delete spr; spr = nullptr; _spr = nullptr; }
  if (!spr) {
    spr = new lgfx::LGFX_Sprite(&gfx);
    spr->setColorDepth(16);
    if (!spr->createSprite(_bw, _bh)) { delete spr; _spr = nullptr; return; }   // OOM -> direct fallback
    _spr = spr; _sprW = _bw; _sprH = _bh;
  }
  spr->fillScreen(kKey);                          // key = transparent (rounded corners show the page)
  drawClock(*spr, 0, 0);
  drawChips(*spr, 0, 0);
  struct tm tm; time_t now = time(nullptr); localtime_r(&now, &tm);
  _shownMin = tm.tm_min; _shownBall = _ball; _shown24 = _use24;
}

void ClockOverlay::stamp(App& app) {
  auto& gfx = app.display().gfx();
  int bx, by; cornerPos(app, _corner, bx, by);

  struct tm tm; time_t now = time(nullptr); localtime_r(&now, &tm);
  bool changed = !_spr || _sprW != _bw || _sprH != _bh ||
                 _shownMin != tm.tm_min || _shownBall != _ball || _shown24 != _use24;
  if (changed) renderSprite(app);

  if (_spr) ((lgfx::LGFX_Sprite*)_spr)->pushSprite(bx, by, kKey);
  else {                                          // sprite OOM: draw straight to the panel
    gfx.startWrite(); drawClock(gfx, bx, by); drawChips(gfx, bx, by); gfx.endWrite();
  }

  const int sy = (by + _gh) - app.contentY(), sy2 = sy + kStripH, hw = _bw / 2;  // chip rects (content-rel)
  _fmtX = bx;      _fmtY = sy;  _fmtW = hw - 1;   _fmtH = kStripH;
  _styX = bx + hw; _styY = sy;  _styW = _bw - hw; _styH = kStripH;
  _autX = bx;      _autY = sy2; _autW = hw - 1;   _autH = kStripH;
  _sclX = bx + hw; _sclY = sy2; _sclW = _bw - hw; _sclH = kStripH;
}

// --- drawing (into a sprite at 0,0, or straight to the panel at x,y) --------
void ClockOverlay::drawClock(lgfx::LovyanGFX& g, int x, int y) {
  if (_ball) drawAnalog(g, x, y); else drawDigits(g, x, y);
}

void ClockOverlay::drawDigits(lgfx::LovyanGFX& g, int x, int y) {
  time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
  char hm[8]; const char* ampm = "";
  if (!_time.synced()) { strncpy(hm, "--:--", sizeof(hm)); }
  else if (_use24) { strftime(hm, sizeof(hm), "%H:%M", &tm); }
  else {
    int h = tm.tm_hour % 12; if (h == 0) h = 12;
    snprintf(hm, sizeof(hm), "%d:%02d", h, tm.tm_min);
    ampm = (tm.tm_hour < 12) ? "AM" : "PM";
  }
  char date[16]; strftime(date, sizeof(date), "%a %b %d", &tm);

  g.fillRoundRect(x, y, _bw, _bh, 6, gTheme.bg);     // opaque card -> legible over anything
  g.drawRoundRect(x, y, _bw, _bh, 6, gTheme.grid);

  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.setTextSize(_scale == 0 ? 3 : _scale == 2 ? 5 : 4);   // scale the time with the box
  g.drawString(hm, x + _bw / 2, y + _gh * 2 / 5);

  char line2[24];
  if (_use24) snprintf(line2, sizeof(line2), "%s", date);
  else        snprintf(line2, sizeof(line2), "%s  %s", ampm, date);
  g.setTextSize(1);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString(line2, x + _bw / 2, y + _gh - 7);
}

void ClockOverlay::drawAnalog(lgfx::LovyanGFX& g, int x, int y) {
  time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
  int cx = x + _bw / 2, cy = y + _gh / 2, r = _gh / 2 - 4;

  g.fillCircle(cx, cy, r, gTheme.bg);           // solid face -> legible "clock ball"
  g.drawCircle(cx, cy, r, gTheme.accent);
  g.drawCircle(cx, cy, r - 1, gTheme.accent);   // 2px rim
  for (int t = 0; t < 12; ++t) {                // hour ticks (bigger dots on the quarters)
    float a = t * (float)M_PI / 6.0f;
    int x0 = cx + (int)lroundf((r - 3) * sinf(a)), y0 = cy - (int)lroundf((r - 3) * cosf(a));
    g.fillCircle(x0, y0, (t % 3 == 0) ? 2 : 1, gTheme.dim);
  }
  char date[12]; strftime(date, sizeof(date), "%b %d", &tm);        // date complication on the face
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextSize(1);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString(date, cx, cy + r / 2);
  float ma = tm.tm_min * (float)M_PI / 30.0f;                       // minute hand
  float ha = (tm.tm_hour % 12 + tm.tm_min / 60.0f) * (float)M_PI / 6.0f;  // hour hand
  g.drawWideLine(cx, cy, cx + (int)lroundf(r * 0.86f * sinf(ma)), cy - (int)lroundf(r * 0.86f * cosf(ma)), 3, gTheme.fg);
  g.drawWideLine(cx, cy, cx + (int)lroundf(r * 0.55f * sinf(ha)), cy - (int)lroundf(r * 0.55f * cosf(ha)), 4, gTheme.fg);
  g.fillCircle(cx, cy, 3, gTheme.accent);
}

void ClockOverlay::drawChips(lgfx::LovyanGFX& g, int x, int y) {
  const int hw = _bw / 2, r1 = y + _gh, r2 = y + _gh + kStripH;   // two chip rows
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextSize(1);
  auto chip = [&](int cx, int cy, int cw, const char* label, Color fg) {
    g.fillRect(cx, cy, cw, kStripH, gTheme.grid);
    g.setTextColor(fg, gTheme.grid);
    g.drawString(label, cx + cw / 2, cy + kStripH / 2);
  };
  static const char* kSz[3] = {"sm", "md", "lg"};
  chip(x,      r1, hw - 1,   _use24 ? "24h" : "AMPM", gTheme.fg);            // format
  chip(x + hw, r1, _bw - hw, _ball ? "clk" : "123",   gTheme.fg);           // digits/analog
  chip(x,      r2, hw - 1,   "auto", _autoUp ? gTheme.ok : gTheme.dim);     // auto-appear
  chip(x + hw, r2, _bw - hw, kSz[_scale < 0 || _scale > 2 ? 1 : _scale], gTheme.fg);  // size
}

bool ClockOverlay::handleTap(App& app, int xRel, int yRel) {
  auto hit = [&](int hx, int hy, int hw, int hh) {
    return xRel >= hx - 4 && xRel <= hx + hw + 4 && yRel >= hy - 4 && yRel <= hy + hh + 4;
  };
  if (hit(_fmtX, _fmtY, _fmtW, _fmtH)) {        // 24h <-> AM/PM
    _use24 = !_use24; _settings.set("clk24", _use24); _settings.save();
    _shownMin = -2;                             // force a sprite re-render
    return true;
  }
  if (hit(_styX, _styY, _styW, _styH)) {        // digits <-> analog (box size changes)
    _ball = !_ball; _settings.set("clkBall", _ball); _settings.save();
    layoutBox(app); _shownMin = -2;
    app.repaintActive();                        // clear the old footprint cleanly
    return true;
  }
  if (hit(_autX, _autY, _autW, _autH)) {        // auto-appear in AUTO mode on/off
    _autoUp = !_autoUp; _settings.set("clkAuto", _autoUp); _settings.save();
    _shownMin = -2;                             // re-render (chip colour changed)
    return true;
  }
  if (hit(_sclX, _sclY, _sclW, _sclH)) {        // size sm -> md -> lg (box size changes)
    _scale = (_scale + 1) % 3; _settings.set("clkScale", (long)_scale); _settings.save();
    layoutBox(app); _shownMin = -2;
    app.repaintActive();
    return true;
  }
  return false;
}
