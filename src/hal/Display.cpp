#include "Display.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <JPEGENC.h>
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
  _jpg = (uint8_t*)malloc(kJpgMax);   // screenshot buffer — once, while the heap is fresh
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

// JPEG-encode the framebuffer MCU-by-MCU (UI thread only — shares the SPI bus with
// the live draw). Buffer is allocated here and freed by the web task after streaming.
void Display::serviceShot() {
  if (!_shotPending) return;
  _shotPending = false;
  _jpgLen = 0;
  if (!_jpg) { _shotReady = true; return; }            // boot allocation failed

  static JPEGENC enc;                                  // static: keep the ~4 KB struct off the stack
  JPEGENCODE jpe;
  int W = _lcd.width(), H = _lcd.height();
  if (enc.open(_jpg, kJpgMax) != JPEGE_SUCCESS ||
      enc.encodeBegin(&jpe, W, H, JPEGE_PIXEL_RGB888, JPEGE_SUBSAMPLE_420, JPEGE_Q_MED) != JPEGE_SUCCESS) {
    _shotReady = true; return;
  }
  static uint16_t blk[16 * 16];
  static uint8_t  mcu[16 * 16 * 3];
  while (jpe.y < H) {
    int bw = jpe.cx, bh = jpe.cy;
    _lcd.readRect(jpe.x, jpe.y, bw, bh, blk);
    for (int yy = 0; yy < bh; ++yy)
      for (int xx = 0; xx < bw; ++xx) {
        // Read-back format: byte-swap, then hi5=B, mid6=R, lo5=G (verified).
        uint16_t c = blk[yy * bw + xx];
        c = (uint16_t)((c >> 8) | (c << 8));
        uint8_t* p = &mcu[(yy * 16 + xx) * 3];   // JPEGENC RGB888 wants B,G,R order
        p[0] = ((c >> 11) & 0x1f) << 3;  // B
        p[1] = (c & 0x1f) << 3;          // G
        p[2] = ((c >> 5) & 0x3f) << 2;   // R
      }
    if (enc.addMCU(&jpe, mcu, 16 * 3) != JPEGE_SUCCESS) break;
  }
  _jpgLen = enc.close();
  _shotReady = true;
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
