#include "Display.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <JPEGENC.h>
#if BACKLIGHT_VIA_EXPANDER
  #include <Wire.h>
#else
  static constexpr int kBlChannel = 7;   // LEDC channel for the backlight PWM
#endif
#if defined(BOARD_CROWPANEL_S3_5HMI)
  #include <esp_lcd_panel_rgb.h>
  #include <esp_lcd_panel_ops.h>

// Per-row hash of the last frame pushed to the panel. flushFramebuffer() pushes only the
// rows that actually changed — exactly what LVGL/cyd-radio do (dirty rectangles). Small
// row-band copies are an imperceptible tear window; pushing the whole 768 KB frame hits the
// PSRAM bandwidth wall (tearing) and stalls the scan (roll). The app draws into LovyanGFX's
// own (off-screen) work framebuffer; only changed rows are copied to esp_lcd's scanned FB.
static uint32_t s_rowHash[TFT_PANEL_HEIGHT];

// esp_lcd RGB panel that OWNS the scan-out (its own framebuffer). LovyanGFX's Bus_RGB scan is
// neutered (patch_lovyangfx.py) and allocates a SEPARATE work framebuffer. data_gpio uses the
// i^8 high/low byte swap to match LovyanGFX's framebuffer byte order.
void Display::rgbPanelBegin() {
  esp_lcd_rgb_panel_config_t pc = {};
  pc.clk_src = LCD_CLK_SRC_PLL160M;
  pc.timings.pclk_hz           = RGB_PCLK_HZ;
  pc.timings.h_res             = TFT_PANEL_WIDTH;
  pc.timings.v_res             = TFT_PANEL_HEIGHT;
  pc.timings.hsync_pulse_width = RGB_HSYNC_PULSE;
  pc.timings.hsync_back_porch  = RGB_HSYNC_BACK;
  pc.timings.hsync_front_porch = RGB_HSYNC_FRONT;
  pc.timings.vsync_pulse_width = RGB_VSYNC_PULSE;
  pc.timings.vsync_back_porch  = RGB_VSYNC_BACK;
  pc.timings.vsync_front_porch = RGB_VSYNC_FRONT;
  pc.timings.flags.pclk_idle_high = 1;       // V1.2 factory setting
  pc.data_width     = 16;
  pc.bits_per_pixel = 16;
  pc.num_fbs        = 1;                      // single FB, like cyd-radio. We push only changed rows
                                              // through a small SRAM staging buffer (flushFramebuffer),
                                              // so each draw_bitmap is tiny -> imperceptible tear, no
                                              // bandwidth wall. No bounce (cyd-radio runs without it).
  pc.hsync_gpio_num = PIN_RGB_HSYNC;
  pc.vsync_gpio_num = PIN_RGB_VSYNC;
  pc.de_gpio_num    = PIN_RGB_DE;
  pc.pclk_gpio_num  = PIN_RGB_PCLK;
  pc.disp_gpio_num  = -1;
  const int dp[16] = { PIN_RGB_D0,PIN_RGB_D1,PIN_RGB_D2,PIN_RGB_D3,PIN_RGB_D4,PIN_RGB_D5,
                       PIN_RGB_D6,PIN_RGB_D7,PIN_RGB_D8,PIN_RGB_D9,PIN_RGB_D10,PIN_RGB_D11,
                       PIN_RGB_D12,PIN_RGB_D13,PIN_RGB_D14,PIN_RGB_D15 };
  for (int i = 0; i < 16; ++i) pc.data_gpio_nums[i] = dp[i ^ 8];   // match LovyanGFX FB byte order
  pc.flags.fb_in_psram = 1;
  esp_lcd_panel_handle_t p = nullptr;
  esp_err_t e = esp_lcd_new_rgb_panel(&pc, &p);
  if (e == ESP_OK) { esp_lcd_panel_reset(p); esp_lcd_panel_init(p); _rgbPanel = p; }
  Serial.printf("[rgb] panel=%d %s (num_fbs=1, dirty-row flush)\n", (int)e, _rgbPanel ? "ready" : "FAILED");
}
#endif

// CrowPanel: push ONLY the rows that changed since last frame to esp_lcd's scanned FB
// (LVGL-style dirty rectangles). Per-row sampled hash detects changes; consecutive dirty
// rows are coalesced into one draw_bitmap. Small copies = imperceptible tear, no roll.
void Display::flushFramebuffer() {
#if defined(BOARD_CROWPANEL_S3_5HMI)
  if (!_rgbPanel) return;
  const uint16_t* cv = (const uint16_t*)_lcd.framebuffer();   // LovyanGFX work FB (the app drew here)
  if (!cv) return;
  const int W = TFT_PANEL_WIDTH, H = TFT_PANEL_HEIGHT;
  // cyd-radio's trick: push only the rows that changed, and stage them through a small INTERNAL
  // SRAM buffer first. draw_bitmap then reads SRAM (not PSRAM) while writing the FB, so it doesn't
  // contend with the continuous scan reading the same PSRAM — that contention is what tore. Each
  // push is at most CHUNK rows (tiny), so any tear window is imperceptible (single FB, no swap).
  static const int CHUNK = 10;
  static uint16_t* stage = (uint16_t*)heap_caps_malloc((size_t)W * CHUNK * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!stage) return;
  int bandStart = -1;
  for (int y = 0; y <= H; ++y) {
    bool dirty = false;
    if (y < H) {
      const uint16_t* row = cv + (size_t)y * W;
      uint32_t h = 2166136261u;                        // FNV-1a over every 4th pixel
      for (int x = 0; x < W; x += 4) h = (h ^ row[x]) * 16777619u;
      if (h != s_rowHash[y]) { s_rowHash[y] = h; dirty = true; }
    }
    if (dirty && bandStart < 0) bandStart = y;
    else if (!dirty && bandStart >= 0) {               // flush the band [bandStart, y) in CHUNK rows
      for (int by = bandStart; by < y; by += CHUNK) {
        int bh = (y - by < CHUNK) ? (y - by) : CHUNK;
        memcpy(stage, cv + (size_t)by * W, (size_t)W * bh * 2);
        esp_lcd_panel_draw_bitmap((esp_lcd_panel_handle_t)_rgbPanel, 0, by, W, by + bh, stage);
      }
      bandStart = -1;
    }
  }
#endif
}

bool Display::begin(bool enableShots) {
  _shotsEnabled = enableShots;

#if defined(BOARD_CROWPANEL_S3_5HMI)
  // esp_lcd owns the scan-out + allocates the framebuffer; do this FIRST so LovyanGFX's
  // (neutered) Bus_RGB can adopt that exact buffer in _lcd.init() and draw straight into it.
  rgbPanelBegin();
#endif

  // LovyanGFX init: on the CrowPanel this adopts esp_lcd's framebuffer + sets up
  // drawing/touch (its RGB scan is neutered — see Bus_RGB.cpp override). On the CYDs this
  // is the full SPI panel init.
  if (!_lcd.init()) return false;

#if BACKLIGHT_VIA_EXPANDER
  // I2C bus + the backlight/reset expander (crowpanel-5in.md §4). Wire owns I2C_NUM_0;
  // LovyanGFX's GT911 is on I2C_NUM_1 to avoid driver contention.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
  expanderBegin();
#endif

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
  if (!_jpg && _shotsEnabled)                                     // lazy: alloc per-shot, freed after serving
#if defined(BOARD_CROWPANEL_S3_5HMI)
    _jpg = (uint8_t*)heap_caps_malloc(kJpgMax, MALLOC_CAP_SPIRAM);  // 160 KB -> PSRAM
#else
    _jpg = (uint8_t*)malloc(kJpgMax);
#endif
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
