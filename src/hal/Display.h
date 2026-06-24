#pragma once
#include <stdint.h>
#include "LGFX_Config.h"

// hal/Display — owns the LovyanGFX device for the active board variant
// (spec §4 hal/Display). Pages never touch this directly; they draw against
// core/Canvas via core/Renderer. Bring-up (milestone 0) is allowed to use
// gfx() directly to validate the panel before the Canvas layer exists.
class Display {
public:
  // Panel init + board-default rotation + backlight on. On the CrowPanel this
  // also brings up the I2C I/O expander that gates the backlight.
  bool begin(bool enableShots = true);

  // LovyanGFX draws into _lcd's framebuffer (its Bus_RGB scan is neutered — see the
  // patch script). On the CrowPanel, esp_lcd owns the actual scan-out and we push
  // _lcd's framebuffer to it each frame in flushFramebuffer().
  // Drawing goes to _drawTarget (default = the panel device). The CrowPanel can redirect it to
  // an INTERNAL-SRAM sprite so a region renders off-PSRAM (no scan contention), then push it.
  lgfx::LovyanGFX& gfx() { return *(_drawTarget ? _drawTarget : static_cast<lgfx::LovyanGFX*>(&_lcd)); }
  void setDrawTarget(lgfx::LovyanGFX* t) { _drawTarget = t; }   // nullptr -> the panel device
  // CrowPanel SRAM renderer: redirect drawing of screen rows [bandY, bandY+bandH) into an INTERNAL-SRAM
  // tile (off the PSRAM bus, no scan contention), then push that tile to the scanned FB. The tile sprite
  // is logically full-screen (so absolute coords aren't clipped) but backed by a small SRAM buffer via an
  // offset pointer + clip = the coordinate translation. No-ops on other boards (draw straight to device).
  void beginTile(int bandY, int bandH);
  void endTile(int bandY, int bandH);
  int  tileRows() const;                  // SRAM tile band height (0 if tiling unavailable)
  // When the whole screen is rendered onto SRAM tiles (status + a tiled content page), the PSRAM
  // work framebuffer is unused, so flushFramebuffer() must not push its stale pixels over the tiles.
  void setContentTiled(bool on) { _contentTiled = on; }
  bool getTouch(int16_t* x, int16_t* y) { return _lcd.getTouch(x, y); }
  void setTouchCalibrate(uint16_t* data) { _lcd.setTouchCalibrate(data); }
  void calibrateTouch(uint16_t* data, uint32_t fg, uint32_t bg, uint8_t size) { _lcd.calibrateTouch(data, fg, bg, size); }

  void setBacklight(uint8_t level);       // 0..255 (spec §7.9 night dimming)
  int  width()  { return _lcd.width(); }
  int  height() { return _lcd.height(); }
  // Runtime screen mirror (horizontal flip, e.g. for a reflected HUD) + colour invert. SPI panels
  // do both in hardware (rotation + INVON); the RGB CrowPanel does them in software at the flush
  // push. mirrored() lets the touch layer flip raw-read coords (GT911) to match.
  void applyDisplayPrefs(bool mirror, bool invert);
  bool mirrored() const { return _mirror; }
  // CrowPanel RGB: hand the freshly-drawn framebuffer to esp_lcd, which double-buffers
  // it (num_fbs=2) and scans via a bounce buffer -> no tearing. No-op on other boards.
  void flushFramebuffer();

  // --- Memory budget helpers (the milestone-0 deliverable, spec §2) ----------
  static uint32_t freeHeap();
  static uint32_t largestFreeBlock();
  static uint32_t psramSize();            // 0 if no PSRAM present

  // --- Debug screenshot (web /api/screen.jpg): full-res JPEG of the framebuffer.
  // serviceShot() (UI thread) reads the panel MCU-by-MCU and JPEG-encodes into a
  // buffer allocated ONCE at boot (fresh, unfragmented heap) so it neither
  // fragments the heap nor competes with TLS allocations at runtime.
  void requestShot() { _shotReady = false; _shotPending = true; }
  void serviceShot();                     // call from the main loop each tick
  void freeShot();                        // release the 16KB buffer after serving (heap floor recovers)
  void setShotsEnabled(bool on) { _shotsEnabled = on; if (!on) freeShot(); }  // runtime enable/disable
  bool shotsEnabled() const { return _shotsEnabled; }
  bool shotReady() const { return _shotReady; }
  const uint8_t* jpeg() const { return _jpg; }
  size_t jpegLen() const { return _jpgLen; }

private:
  int encodeJpeg(int quality);            // -> JPEG size in _jpg, or 0 if it overflowed
  LGFX _lcd;
  lgfx::LovyanGFX* _drawTarget = nullptr; // current draw target (nullptr -> &_lcd)
  bool _contentTiled = false;             // true when status+content are on SRAM tiles (skip work-FB push)
  bool _mirror = false, _invert = false;  // runtime display prefs (RGB board applies these at flush)
#if defined(BOARD_CROWPANEL_S3_5HMI)
  void* _rgbPanel = nullptr;              // esp_lcd_panel_handle_t (owns scan)
  void  rgbPanelBegin();                  // create + start the esp_lcd RGB panel
  lgfx::LGFX_Sprite _tile{ &_lcd };       // INTERNAL-SRAM render tile (off-PSRAM), repositioned per band
  uint8_t* _tileBuf = nullptr;            // the tile's SRAM buffer (TFT_PANEL_WIDTH x kTileRows)
#endif
#if defined(BOARD_CROWPANEL_S3_5HMI)
  static constexpr int kJpgMax = 160000;  // 800x480 is 5x the CYD's pixels -> bigger JPEG (PSRAM has room)
#else
  static constexpr int kJpgMax = 16000;   // output cap (keeps largest free block > TLS floor)
#endif
  uint8_t* _jpg = nullptr;
  size_t   _jpgLen = 0;
  bool     _shotsEnabled = true;          // gate the 16KB buffer (production frees it)
  volatile bool _shotPending = false;
  volatile bool _shotReady = false;
#if BACKLIGHT_VIA_EXPANDER
  uint8_t _expanderAddr = 0;              // detected I2C expander (0 = none)
  void    expanderBegin();
#endif
};
