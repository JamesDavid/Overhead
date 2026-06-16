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
  bool begin();

  LGFX& gfx() { return _lcd; }            // raw device (bring-up / Renderer)

  void setBacklight(uint8_t level);       // 0..255 (spec §7.9 night dimming)
  int  width()  { return _lcd.width(); }
  int  height() { return _lcd.height(); }

  // --- Memory budget helpers (the milestone-0 deliverable, spec §2) ----------
  static uint32_t freeHeap();
  static uint32_t largestFreeBlock();
  static uint32_t psramSize();            // 0 if no PSRAM present

  // --- Debug screenshot (web /api/screen): full-res framebuffer read-back, one
  // row at a time. The web (async) task requests row y; serviceShot() does the SPI
  // read on the UI thread (so it never races the live draw) into rowBuf().
  void requestRow(int y) { _rowReady = false; _rowReq = y; }
  void serviceShot();                     // call from the main loop each tick
  bool rowReady() const { return _rowReady; }
  const uint16_t* rowBuf() const { return _rowBuf; }

private:
  LGFX _lcd;
  volatile int  _rowReq = -1;
  volatile bool _rowReady = false;
  uint16_t _rowBuf[480];                   // one scanline (max panel width)
#if BACKLIGHT_VIA_EXPANDER
  uint8_t _expanderAddr = 0;              // detected I2C expander (0 = none)
  void    expanderBegin();
#endif
};
