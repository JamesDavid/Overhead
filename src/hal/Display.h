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

  // --- Debug screenshot (web /api/screen): a downsampled framebuffer read-back,
  // captured in ONE pass on the UI thread (never races the live draw, never blocks
  // the async web task). requestShot() flags it; serviceShot() captures; shot()
  // exposes the RGB565 buffer.
  static constexpr int kShotW = 160, kShotH = 120;
  void requestShot() { _shotReady = false; _shotPending = true; }
  void serviceShot();                     // call from the main loop each tick
  bool shotReady() const { return _shotReady; }
  const uint16_t* shot() const { return _shot; }

private:
  LGFX _lcd;
  uint16_t* _shot = nullptr;
  volatile bool _shotPending = false;
  bool _shotReady = false;
#if BACKLIGHT_VIA_EXPANDER
  uint8_t _expanderAddr = 0;              // detected I2C expander (0 = none)
  void    expanderBegin();
#endif
};
