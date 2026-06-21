#include "Display.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <JPEGENC.h>
#if BACKLIGHT_VIA_EXPANDER
  #include <Wire.h>
#else
  static constexpr int kBlChannel = 7;   // LEDC channel for the backlight PWM
#endif
// RGB panel double-buffer: copy the finished canvas into the framebuffer that is NOT
// being scanned, then ask the panel to swap to it at the next vblank. The scanned buffer
// is never written, so the panel never shows a torn frame.
void Display::flushFramebuffer() {
#if defined(BOARD_CROWPANEL_S3_5HMI)
  if (!_canvas.getBuffer() || !_fbA || !_fbB) return;
  size_t len = (size_t)_lcd.width() * _lcd.height() * 2;
  // Pick the framebuffer that ISN'T currently being scanned (use the live scan pointer,
  // which the VSYNC ISR updates, so a not-yet-applied swap can't make us write the front).
  uint8_t* front = _lcd.framebuffer();
  uint8_t* back  = (front == _fbA) ? _fbB : _fbA;
  memcpy(back, _canvas.getBuffer(), len);   // write the OFF-screen buffer (never torn)
  _lcd.setScanBuffer(back);                  // panel swaps to it at the next vblank
  _scanFront = back;
#endif
}

bool Display::begin(bool enableShots) {
  _shotsEnabled = enableShots;

  // LovyanGFX init: on the CrowPanel this only allocates the framebuffer + sets up
  // drawing/touch (its RGB scan is neutered — see Bus_RGB.cpp override); esp_lcd does
  // the actual scan-out below. On the CYDs this is the full SPI panel init.
  if (!_lcd.init()) return false;

#if BACKLIGHT_VIA_EXPANDER
  // I2C bus + the backlight/reset expander (crowpanel-5in.md §4). Wire owns I2C_NUM_0;
  // LovyanGFX's GT911 is on I2C_NUM_1 to avoid driver contention.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
  expanderBegin();
#endif

  _lcd.setRotation(DISPLAY_DEFAULT_ROTATION);
  _lcd.fillScreen(TFT_BLACK);
#if defined(BOARD_CROWPANEL_S3_5HMI)
  // Off-screen canvas (full-screen, PSRAM) that all drawing targets, plus a second scan
  // framebuffer for true double-buffering: flushFramebuffer() copies the finished canvas
  // into whichever framebuffer ISN'T being scanned, then swaps the scan to it at vblank
  // -> the panel never shows a half-updated frame (no tearing).
  _canvas.setColorDepth(16);
  _canvas.setPsram(true);
  if (!_canvas.createSprite(_lcd.width(), _lcd.height()))
    Serial.println("[disp] canvas sprite alloc FAILED");
  else
    _canvas.fillScreen(TFT_BLACK);
  size_t fbLen = (size_t)_lcd.width() * _lcd.height() * 2;
  _fbA = _lcd.framebuffer();                                   // Bus_RGB's framebuffer (initial scan)
  _fbB = (uint8_t*)heap_caps_malloc(fbLen, MALLOC_CAP_SPIRAM); // second scan buffer
  if (_fbB) memset(_fbB, 0, fbLen);
  _scanFront = _fbA;
  Serial.printf("[disp] double-buffer A=%p B=%p\n", _fbA, _fbB);
#endif
#if !BACKLIGHT_VIA_EXPANDER
  // Drive the backlight PWM ourselves (LovyanGFX's Light_PWM didn't actually vary
  // brightness on the cyd28 unit — stuck dim). Same channel as the LGFX config;
  // re-attaching after init() hands control to us. Core 2.x ledc API (the CYDs).
  ledcSetup(kBlChannel, BACKLIGHT_PWM_FREQ, 8);
  ledcAttachPin(PIN_TFT_BL, kBlChannel);
#endif
  setBacklight(255);
#if defined(BOARD_CROWPANEL_S3_5HMI)
  Serial.printf("[disp] psram=%d psramSize=%u freePsram=%u  panel=%dx%d\n",
                (int)psramFound(), (unsigned)ESP.getPsramSize(),
                (unsigned)ESP.getFreePsram(), _lcd.width(), _lcd.height());
#endif
  // Screenshot buffer is allocated LAZILY on the first screenshot request (see
  // serviceShot), not here. On a no-PSRAM board this 16 KB sits in the band TLS
  // needs, so deferring it until a screenshot is actually taken keeps the heap
  // floor clear for HTTPS — and a lean updater boot (no screenshots) never pays
  // for it at all.
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

// Encode the framebuffer at a given JPEG quality into _jpg; returns the byte size,
// or 0 if it overflowed the buffer (so the caller can retry at lower quality).
int Display::encodeJpeg(int quality) {
  static JPEGENC enc;                                  // static: keep the ~4 KB struct off the stack
  JPEGENCODE jpe;
  int W = _lcd.width(), H = _lcd.height();
  if (enc.open(_jpg, kJpgMax) != JPEGE_SUCCESS ||
      enc.encodeBegin(&jpe, W, H, JPEGE_PIXEL_RGB888, JPEGE_SUBSAMPLE_420, (uint8_t)quality) != JPEGE_SUCCESS)
    return 0;
  static uint16_t blk[16 * 16];
  static uint8_t  mcu[16 * 16 * 3];
  bool ok = true;
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
    if (enc.addMCU(&jpe, mcu, 16 * 3) != JPEGE_SUCCESS) { ok = false; break; }  // buffer full
  }
  int sz = enc.close();
  return (ok && jpe.y >= H) ? sz : 0;     // 0 = overflowed/incomplete -> retry lower quality
}

// UI thread only (shares the SPI bus with the live draw). Busy screens (big T-minus,
// dense lists) blow past the buffer at medium quality, so fall back to low.
void Display::serviceShot() {
  if (!_shotPending) return;
  _shotPending = false;
  _jpgLen = 0;
  if (!_jpg && _shotsEnabled) _jpg = (uint8_t*)malloc(kJpgMax);   // lazy: alloc per-shot, freed after serving
  if (!_jpg) { _shotReady = true; return; }            // disabled or allocation failed
  _jpgLen = encodeJpeg(JPEGE_Q_MED);
  if (_jpgLen == 0) _jpgLen = encodeJpeg(JPEGE_Q_LOW);
  _shotReady = true;
}

void Display::freeShot() {                  // release the 16KB buffer between shots -> heap floor recovers
  if (_jpg) { free(_jpg); _jpg = nullptr; }
  _jpgLen = 0; _shotReady = false;
}

void Display::setBacklight(uint8_t level) {
#if BACKLIGHT_VIA_EXPANDER
  if (!_expanderAddr) return;
  if (_expanderAddr == I2C_ADDR_EXP_STC8) {
    // V1.2 STC8: the byte IS a brightness level, not a command code — 0 = max,
    // 244 = min, 245 = off (Elecrow wiki). (V1.1 used command codes 0x10=on /
    // 0x05=off; V1.2 superseded them and is the board on hand.) Map our 0..255
    // (255 = full brightness) onto the inverted 0..244 scale.
    uint8_t v = level ? (uint8_t)(244 - (uint32_t)level * 244 / 255) : 245;
    Wire.beginTransmission(_expanderAddr);
    Wire.write(v);
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
