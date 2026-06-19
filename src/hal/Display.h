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

  LGFX& gfx() { return _lcd; }            // raw device (bring-up / Renderer)

  void setBacklight(uint8_t level);       // 0..255 (spec §7.9 night dimming)
  int  width()  { return _lcd.width(); }
  int  height() { return _lcd.height(); }

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
  bool shotReady() const { return _shotReady; }
  const uint8_t* jpeg() const { return _jpg; }
  size_t jpegLen() const { return _jpgLen; }

private:
  int encodeJpeg(int quality);            // -> JPEG size in _jpg, or 0 if it overflowed
  LGFX _lcd;
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
