#include "Display.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#if BACKLIGHT_VIA_EXPANDER
  #include <Wire.h>
#else
  static constexpr int kBlChannel = 7;   // LEDC channel for the backlight PWM
#endif

bool Display::begin() {
#if BACKLIGHT_VIA_EXPANDER
  // CrowPanel: the I2C expander gates LCD reset + backlight, so it must come
  // up before/around the panel (crowpanel-5in.md §4). Wire owns I2C_NUM_0;
  // LovyanGFX's GT911 is configured on I2C_NUM_1 to avoid driver contention.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
  expanderBegin();
#endif

  if (!_lcd.init()) return false;

  _lcd.setRotation(DISPLAY_DEFAULT_ROTATION);
  _lcd.fillScreen(TFT_BLACK);
#if !BACKLIGHT_VIA_EXPANDER
  // Drive the backlight PWM ourselves (LovyanGFX's Light_PWM didn't actually vary
  // brightness on the cyd28 unit — stuck dim). Same channel as the LGFX config;
  // re-attaching after init() hands control to us. Core 2.x ledc API (the CYDs).
  ledcSetup(kBlChannel, BACKLIGHT_PWM_FREQ, 8);
  ledcAttachPin(PIN_TFT_BL, kBlChannel);
#endif
  setBacklight(255);
  return true;
}

#if BACKLIGHT_VIA_EXPANDER
void Display::expanderBegin() {
  // Probe the V1.1+ STC8 (0x30) first, then the V1.0 TCA9534 (0x18); remember
  // whichever ACKs so the same firmware runs on either revision (doc §9).
  for (uint8_t addr : {(uint8_t)I2C_ADDR_EXP_STC8, (uint8_t)I2C_ADDR_EXP_TCA9534}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) { _expanderAddr = addr; break; }
  }
  if (!_expanderAddr) { Serial.println("[exp] no I2C expander found!"); return; }
  Serial.printf("[exp] expander @ 0x%02X\n", _expanderAddr);

  if (_expanderAddr == I2C_ADDR_EXP_STC8) {
    Wire.beginTransmission(_expanderAddr);
    Wire.write(STC8_CMD_AMP_MUTE);          // keep the amp muted (no audio here)
    Wire.endTransmission();
  } else {
    // TCA9534: config reg 0x03 = all outputs, output reg 0x01 = all high
    // (P3 backlight active-high, P4 amp-shutdown high = muted).
    Wire.beginTransmission(_expanderAddr); Wire.write(0x03); Wire.write(0x00); Wire.endTransmission();
    Wire.beginTransmission(_expanderAddr); Wire.write(0x01); Wire.write(0xFF); Wire.endTransmission();
  }
}
#endif

// Read one requested scanline from the framebuffer (UI thread only — shares the
// SPI bus with the live draw, so it must not run from the async web task).
void Display::serviceShot() {
  if (_rowReq < 0) return;
  int y = _rowReq, w = _lcd.width();
  if (w > 480) w = 480;
  if (y >= 0 && y < _lcd.height()) _lcd.readRect(0, y, w, 1, _rowBuf);
  _rowReady = true; _rowReq = -1;
}

void Display::setBacklight(uint8_t level) {
#if BACKLIGHT_VIA_EXPANDER
  if (!_expanderAddr) return;
  if (_expanderAddr == I2C_ADDR_EXP_STC8) {
    // STC8 has coarse levels; a dim byte alone blanks the panel, so 0x10 must
    // precede it (doc §7). Bring-up only needs on/off granularity.
    Wire.beginTransmission(_expanderAddr);
    Wire.write(level ? STC8_CMD_BL_MAX : STC8_CMD_BL_OFF);
    Wire.endTransmission();
  } else {
    Wire.beginTransmission(_expanderAddr);
    Wire.write(0x01);
    Wire.write(level ? 0xFF : (0xFF & ~0x08));   // toggle P3 (backlight)
    Wire.endTransmission();
  }
#else
  ledcWrite(kBlChannel, BACKLIGHT_ACTIVE_HIGH ? level : 255 - level);
#endif
}

uint32_t Display::freeHeap() {
  return ESP.getFreeHeap();
}

uint32_t Display::largestFreeBlock() {
  return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

uint32_t Display::psramSize() {
  return ESP.getPsramSize();   // 0 when no PSRAM is fitted
}
