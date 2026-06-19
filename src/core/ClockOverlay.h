#pragma once
#include <stdint.h>

class App;
class TimeService;
class Settings;
namespace lgfx { inline namespace v1 { class LovyanGFX; } }

// core/ClockOverlay — a device-wide "clock mode" (tap the time in the status
// strip). The active page keeps running live; a big clock is stamped on top in
// a corner each frame. Nothing is read back from the panel and the page is never
// force-redrawn per frame, so there is neither scramble nor strobe.
//
//  * Live pages (sat pass, aircraft, launches, agenda — Page::clockKeepLive())
//    park the clock STATIC in the lower-right corner so you can watch the data.
//  * Calm pages (star map, orbits, ...) hop the clock to a random corner every
//    ~10 s for burn-in protection; each hop triggers one clean page repaint so
//    the old corner is cleared without any per-frame redrawing.
//
// Two style toggles via small bottom-left chips: 24-hour vs AM/PM, and a digital
// readout vs a small analog clock face (with a date complication).
class ClockOverlay {
public:
  ClockOverlay(TimeService& time, Settings& settings) : _time(time), _settings(settings) {}

  void begin();                       // load style prefs from Settings
  bool active() const { return _on; }
  bool autoUp() const { return _autoUp; }   // should the clock auto-appear when idle in AUTO?
  void toggle(App& app);              // turn clock mode on/off

  // Per-frame hooks, called by App: prepare() picks the corner (and repaints the
  // page on a hop) BEFORE the page ticks; stamp() draws the clock on top after.
  void prepare(App& app, uint32_t now, bool live);
  void stamp(App& app);
  void invalidate() { _firstFrame = true; }   // page switched -> re-pick corner + repaint

  // Content-area tap while clock mode is on. Returns true if a chip was hit
  // (setting toggled); false means "tapped elsewhere" -> caller turns mode off.
  bool handleTap(App& app, int xRel, int yRel);

private:
  void layoutBox(App& app);           // set box w/h for the current style
  void cornerPos(App& app, int corner, int& x, int& y);
  int  pickCorner();                  // a random corner != current (avoids the chip row)
  void renderSprite(App& app);        // (re)draw the clock into the off-screen sprite
  void drawClock(lgfx::LovyanGFX& g, int x, int y);
  void drawDigits(lgfx::LovyanGFX& g, int x, int y);
  void drawAnalog(lgfx::LovyanGFX& g, int x, int y);
  void drawChips(lgfx::LovyanGFX& g, int x, int y);   // built-in chip strip at the box bottom

  TimeService& _time;
  Settings&    _settings;

  bool  _on    = false;
  bool  _use24 = true;                // false = 12-hour AM/PM
  bool  _ball  = false;               // false = digits, true = analog face
  int   _scale = 1;                   // 0 small, 1 medium, 2 large
  bool  _autoUp = false;              // auto-appear when idle in AUTO mode
  int   _bw = 0, _bh = 0;             // clock box size (graphic + chip strip)
  int   _gh = 0;                      // graphic height (box height minus the chip strip)

  int   _corner = 3;                  // 0 TL, 1 TR, 3 BR (2 BL avoided: chip row)
  uint32_t _cornerMs = 0;             // last hop time (calm pages)
  bool  _firstFrame = true;
  uint32_t _rng = 0x2545F491;         // tiny LCG for corner choice

  // Off-screen sprite: the clock is rendered once (on a content change) and blitted
  // each frame, so there's no per-frame primitive-by-primitive redraw flicker.
  void* _spr = nullptr;               // lgfx::LGFX_Sprite* (void to keep LovyanGFX out of the header)
  int   _sprW = 0, _sprH = 0;         // current sprite size
  int   _shownMin = -2;               // minute rendered into the sprite (re-render on change)
  bool  _shownBall = false, _shown24 = true;

  // Chip hit rects (content-relative), set by stamp(). Two rows of two chips.
  int _fmtX = 0, _fmtY = 0, _fmtW = 0, _fmtH = 0;   // 24h / AM-PM   (row 1 left)
  int _styX = 0, _styY = 0, _styW = 0, _styH = 0;   // digits / analog (row 1 right)
  int _autX = 0, _autY = 0, _autW = 0, _autH = 0;   // auto-up toggle (row 2 left)
  int _sclX = 0, _sclY = 0, _sclW = 0, _sclH = 0;   // size sm/md/lg  (row 2 right)
};
