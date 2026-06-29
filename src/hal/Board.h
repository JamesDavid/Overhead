#pragma once
// hal/Board.h — per-variant pins + capability flags (spec §2, §3.1).
//
// Selected by a -D BOARD_* build flag in platformio.ini. ALL board-specific
// constants live here; the rest of the codebase is variant-agnostic and reads
// only the names defined below. Adding a board = a new block here + a new env.
//
// Capability contract every variant must define:
//   BOARD_NAME            human string
//   CAP_HAS_PSRAM         0/1
//   CAP_HAS_RTC           0/1   (onboard battery-backed RTC)
//   CAP_HAS_GPS           0/1
//   CAP_TOUCH_NEEDS_CAL   0/1   (resistive needs 4-corner cal; capacitive 0)
//   BACKLIGHT_VIA_EXPANDER 0/1  (1 = backlight is behind an I2C expander)
// Panel wiring (SPI vs parallel-RGB) lives in hal/LGFX_Config.h, branched on
// the same BOARD_* flag.

// ===========================================================================
#if defined(BOARD_CYD4_ST7796)
// ===========================================================================
// 4" Cheap Yellow Display — "ESP32-32E 4 inch", ST7796S 480x320, XPT2046
// resistive touch sharing the LCD SPI bus. No PSRAM.
// Pinout: https://www.lcdwiki.com/4.0inch_ESP32-32E_Display

  #define BOARD_NAME            "CYD 4\" ST7796 (ESP32-32E)"

  // Display (ST7796S over HSPI: GPIO 12/13/14 are native HSPI pins)
  #define PIN_TFT_MOSI          13
  #define PIN_TFT_MISO          12
  #define PIN_TFT_SCLK          14
  #define PIN_TFT_CS            15
  #define PIN_TFT_DC             2
  #define PIN_TFT_RST           -1    // tied to EN / system reset
  #define PIN_TFT_BL            27    // backlight, active HIGH (NOT 21 — that's
                                      // the 2.8" board; here 21 is on a header)
  #define TFT_PANEL_WIDTH      320    // native portrait; landscape = rotation 1
  #define TFT_PANEL_HEIGHT     480
  #define DISPLAY_DEFAULT_ROTATION 1  // 480x320 landscape

  // Touch (XPT2046) — shares MOSI/MISO/SCLK with the LCD; own CS + IRQ
  #define PIN_TOUCH_CS          33
  #define PIN_TOUCH_IRQ         36    // input-only

  // microSD (separate VSPI bus: 18/23/19) — not used in bring-up
  #define PIN_SD_CS              5
  #define PIN_SD_SCLK           18
  #define PIN_SD_MOSI           23
  #define PIN_SD_MISO           19

  // Backlight (cyd.md: low PWM freq — the MOSFET can't switch off above ~10 kHz)
  #define BACKLIGHT_ACTIVE_HIGH  1
  #define BACKLIGHT_PWM_FREQ  5000
  #define BACKLIGHT_VIA_EXPANDER 0

  // Capabilities. PSRAM is unconfirmed for ESP32-32E (none documented); the
  // milestone-0 heap print is ground truth. RTC optional on a header (spec §2).
  #define CAP_HAS_PSRAM          0
  #define CAP_HAS_RTC            0
  #define CAP_HAS_GPS            0
  #define CAP_TOUCH_NEEDS_CAL    1   // resistive

// ===========================================================================
#elif defined(BOARD_CYD28_ILI9341)
// ===========================================================================
// 2.8" Cheap Yellow Display — "ESP32-2432S028R", ILI9341 240x320 over HSPI,
// XPT2046 resistive touch on a SEPARATE VSPI bus. No PSRAM.
// Pinout + quirks: ../BladeAir/cyd.md

  // Variant: the Elegoo "USB-C only" CYD reuses this whole block (same pins/driver) but overrides
  // the panel orientation + touch-invert below via -D flags (see env cyd28_elegoo in platformio.ini).
#ifdef BOARD_CYD28_ELEGOO
  #define BOARD_NAME            "CYD 2.8\" ILI9341 USB-C (Elegoo)"
#else
  #define BOARD_NAME            "CYD 2.8\" ILI9341 (ESP32-2432S028R)"
#endif

  // Display (ILI9341 over HSPI: GPIO 12/13/14)
  #define PIN_TFT_MOSI          13
  #define PIN_TFT_MISO          12
  #define PIN_TFT_SCLK          14
  #define PIN_TFT_CS            15
  #define PIN_TFT_DC             2
  #define PIN_TFT_RST           -1    // tied to system reset
  #define PIN_TFT_BL            21    // backlight, active HIGH
  // ORIENTATION — the hard-won fix (see ../Voxalon/yoradio-pio
  // yoRadio/src/displays/displayILI9341.cpp + boards/cyd_common.h):
  //   This panel's ILI9341 is mounted so the chip's MV=1 "landscape" modes
  //   (LovyanGFX ODD rotations) render 90deg-rotated and can only differ by
  //   180deg mirror bits — they can NEVER produce the 90deg we need. The
  //   orientation that visually appears as correct landscape is the chip's MV=0
  //   mode, and on this CYD the column range extends so a full 320-wide layout
  //   still fits. So we drive the panel as LANDSCAPE-NATIVE 320x240 at MV=0
  //   (rotation 0; rotation 2 = 180 flip), NOT via a swapping odd rotation.
  // -028R is driven landscape-native (320x240, even rotations). The Elegoo USB-C variant has the
  // standard portrait-native ILI9341, so it overrides these to 240x320 + rotation 1 (odd = landscape).
#ifndef TFT_PANEL_WIDTH
  #define TFT_PANEL_WIDTH      320
#endif
#ifndef TFT_PANEL_HEIGHT
  #define TFT_PANEL_HEIGHT     240
#endif
  // MV=0 family (even rotations, all report 320x240): rot 0 was upright but
  // horizontally mirrored on this unit, so use rot 6 = rot 0 + horizontal flip
  // (upright, un-mirrored). Reference for the other MV=0 values if needed:
  // 0 = upright+mirrored, 2 = upside-down+un-mirrored, 4 = upside-down+mirrored.
#ifndef DISPLAY_DEFAULT_ROTATION
  #define DISPLAY_DEFAULT_ROTATION 6
#endif
  // COLOUR — this CYD has reversed R/B wiring; without RGB order the warm theme
  // colours (yellow/orange) render as cyan/blue (yoRadio DSP_PANEL_RGB). Set 0
  // if your unit's colours are already correct.
  #define CYD_PANEL_RGB_ORDER  1
  // Changing rotation auto-invalidates the saved touch calibration (Touch.cpp).

  // Touch (XPT2046) on its OWN VSPI bus (cyd.md) — separate sclk/mosi/miso
  #define PIN_TOUCH_SCLK        25
  #define PIN_TOUCH_MOSI        32    // T_DIN
  #define PIN_TOUCH_MISO        39    // T_OUT (input-only)
  #define PIN_TOUCH_CS          33
  #define PIN_TOUCH_IRQ         36    // input-only
  // The MV=0 landscape flip (rotation 6) makes LovyanGFX's calibrateTouch draw
  // its corner targets at physically-flipped positions, so the learned map comes
  // out 180 deg point-reflected. Invert both calibrated axes to correct it.
  // -028R MV=0 landscape flips calibrateTouch's targets -> invert both axes. The Elegoo USB-C's
  // standard MV=1 landscape needs no inversion, so it overrides both to 0.
#ifndef TOUCH_INVERT_X
  #define TOUCH_INVERT_X        1
#endif
#ifndef TOUCH_INVERT_Y
  #define TOUCH_INVERT_Y        1
#endif

  // microSD (HSPI, shared with display: 18/23/19) — not used in bring-up
  #define PIN_SD_CS              5
  #define PIN_SD_SCLK           18
  #define PIN_SD_MOSI           23
  #define PIN_SD_MISO           19

  // Backlight (cyd.md: low PWM freq — MOSFET can't switch off above ~10 kHz)
  #define BACKLIGHT_ACTIVE_HIGH  1
  #define BACKLIGHT_PWM_FREQ  5000
  #define BACKLIGHT_VIA_EXPANDER 0

  #define CAP_HAS_PSRAM          0
  #define CAP_HAS_RTC            0
  #define CAP_HAS_GPS            0
  #define CAP_TOUCH_NEEDS_CAL    1   // resistive

  // Onboard speaker on GPIO26 (via transistor) -- PWM tone for the Morse alert beeper.
  #ifndef BUZZER_PIN
  #define BUZZER_PIN            26
  #endif
  #ifndef BUZZER_FREQ
  #define BUZZER_FREQ          650   // pleasant Morse pitch on the small speaker
  #endif

// ===========================================================================
#elif defined(BOARD_CROWPANEL_S3_5HMI)
// ===========================================================================
// Elecrow CrowPanel Advance 5.0-HMI — ESP32-S3-WROOM-1-N16R8 (16 MB flash,
// 8 MB octal PSRAM), 800x480 IPS, 16-bit parallel RGB (ST7262), GT911
// capacitive touch over I2C, backlight behind an I2C I/O expander.
// Pins from ../cyd-radio/crowpanel-5in.md (silkscreen + V1.0 factory source —
// authoritative; the Elecrow public wiki lists a DIFFERENT, wrong pinout).

  #define BOARD_NAME            "CrowPanel Advance 5.0-HMI (ESP32-S3)"

  #define TFT_PANEL_WIDTH      800    // native landscape
  #define TFT_PANEL_HEIGHT     480
  #define DISPLAY_DEFAULT_ROTATION 0  // already landscape

  // RGB parallel data pins, RGB565 order d0..d15 = B0..B4, G0..G5, R0..R4.
  #define PIN_RGB_D0   21  // B0
  #define PIN_RGB_D1   47  // B1
  #define PIN_RGB_D2   48  // B2
  #define PIN_RGB_D3   45  // B3
  #define PIN_RGB_D4   38  // B4
  #define PIN_RGB_D5    9  // G0
  #define PIN_RGB_D6   10  // G1
  #define PIN_RGB_D7   11  // G2
  #define PIN_RGB_D8   12  // G3
  #define PIN_RGB_D9   13  // G4
  #define PIN_RGB_D10  14  // G5
  #define PIN_RGB_D11   7  // R0
  #define PIN_RGB_D12  17  // R1
  #define PIN_RGB_D13  18  // R2
  #define PIN_RGB_D14   3  // R3
  #define PIN_RGB_D15  46  // R4
  #define PIN_RGB_PCLK  39
  #define PIN_RGB_HSYNC 40
  #define PIN_RGB_VSYNC 41
  #define PIN_RGB_DE    42
  // Pixel clock: 12 MHz is the doc's documented-stable value (factory uses
  // Values from the Elecrow V1.2 factory LovyanGFX driver (which works on THIS board):
  // 21 MHz pclk + pclk_idle_high=1. (cyd-radio's V1.1 ran 12 MHz / pclk_active_neg, but
  // the V1.2 ST7262 needs the factory 21 MHz + idle-high or the panel never latches —
  // stays black though the framebuffer is correct.) Porches 8/4/8 both axes.
  #define RGB_PCLK_HZ        21000000
  #define RGB_HSYNC_FRONT    8
  #define RGB_HSYNC_PULSE    4
  #define RGB_HSYNC_BACK     8
  #define RGB_VSYNC_FRONT    8
  #define RGB_VSYNC_PULSE    4
  #define RGB_VSYNC_BACK     8

  // Shared I2C bus (400 kHz): GT911 touch, RTC, I/O expander
  #define PIN_I2C_SDA          15
  #define PIN_I2C_SCL          16
  #define I2C_FREQ_HZ          400000
  #define I2C_ADDR_GT911       0x5D
  #define I2C_ADDR_RTC         0x51   // PCF8563 / BM8563
  #define I2C_ADDR_EXP_STC8    0x30   // V1.1+ µC (single-byte commands)
  #define I2C_ADDR_EXP_TCA9534 0x18   // V1.0 expander
  // STC8 single-byte commands (crowpanel-5in.md §7)
  #define STC8_CMD_BL_OFF      0x05
  #define STC8_CMD_BL_MAX      0x10   // must precede any dim byte (0x06..0x09)
  #define STC8_CMD_AMP_MUTE    0x18

  // Touch GT911 reset/INT are handled by the expander; no direct GPIO.
  #define PIN_TOUCH_INT        -1
  #define PIN_TOUCH_RST        -1

  #define BACKLIGHT_VIA_EXPANDER 1

  // Capabilities — confirmed by the reference doc (spec §3.1).
  #define CAP_HAS_PSRAM          1   // 8 MB octal/OPI
  #define CAP_HAS_RTC            1   // PCF8563 onboard
  #define CAP_HAS_GPS            0
  #define CAP_TOUCH_NEEDS_CAL    0   // capacitive

  // V1.2 buzzer + speaker are NOT raw GPIOs -- they're driven by the STC8 expander (0x30),
  // the same single-command-byte channel as the backlight (crowpanel-5in.md): 246=buzzer on,
  // 247=buzzer off, 248=speaker on, 249=speaker off. The Morse beeper keys the (active) buzzer
  // on/off with these. (V1.1 used 0x15/0x16; this board is V1.2.)
  #define BUZZER_VIA_EXPANDER    1
  #define STC8_CMD_BUZZER_ON     246
  #define STC8_CMD_BUZZER_OFF    247
  #define STC8_CMD_SPEAKER_ON    248
  #define STC8_CMD_SPEAKER_OFF   249

// ===========================================================================
#else
  #error "No board variant selected. Define BOARD_CYD4_ST7796 or BOARD_CROWPANEL_S3_5HMI in platformio.ini build_flags."
#endif
