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

#if defined(BOARD_CROWPANEL_S3_5HMI)
  // RGB panel: draw into an off-screen full-screen sprite, then push it to the panel
  // in one shot each frame (flushFramebuffer). Drawing straight into the scanned
  // framebuffer tears badly ("scrambled"); this collapses it to a single block update.
  lgfx::LovyanGFX& gfx() { return _canvas; }
#else
  LGFX& gfx() { return _lcd; }            // raw device (bring-up / Renderer)
#endif
  // Touch lives on the real device even when gfx() is the off-screen canvas.
  bool getTouch(int16_t* x, int16_t* y) { return _lcd.getTouch(x, y); }
  void setTouchCalibrate(uint16_t* data) { _lcd.setTouchCalibrate(data); }

  void setBacklight(uint8_t level);       // 0..255 (spec §7.9 night dimming)
  int  width()  { return _lcd.width(); }
  int  height() { return _lcd.height(); }
  // RGB panels only: flush the framebuffer cache to PSRAM so the DMA scan-out sees
  // the latest pixels (no-op where the panel isn't a cached PSRAM framebuffer).
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
#if defined(BOARD_CROWPANEL_S3_5HMI)
  lgfx::LGFX_Sprite _canvas{ &_lcd };     // off-screen draw target (drawn by the app)
  uint8_t* _fbA = nullptr;                // the two scan framebuffers (double-buffer)
  uint8_t* _fbB = nullptr;
  uint8_t* _scanFront = nullptr;          // which one the panel is currently scanning
#endif
  static constexpr int kJpgMax = 16000;   // output cap (keeps largest free block > TLS floor)
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
