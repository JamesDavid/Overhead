#pragma once
// hal/LGFX_Config.h — concrete LovyanGFX device for the active board variant.
//
// This is the ONE place that knows the panel/bus/touch wiring of the display
// library. hal/Display wraps an instance of this; everything above the HAL
// draws against core/Canvas, never this type directly. One renderer
// (LovyanGFX) drives BOTH boards — only this config differs (spec §3.2: "Pages
// draw against our abstract Canvas either way, so it's reversible").

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "Board.h"
#include "config.h"

// ===========================================================================
#if defined(BOARD_CYD4_ST7796)
// ===========================================================================
// ST7796S 480x320 over SPI; XPT2046 resistive touch on the SAME (shared) bus.

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796   _panel;
  lgfx::Bus_SPI        _bus;
  lgfx::Light_PWM      _light;
  lgfx::Touch_XPT2046  _touch;

public:
  LGFX() {
    {   // SPI bus (HSPI: pins 12/13/14 are the ESP32 native HSPI pins)
      auto cfg = _bus.config();
      cfg.spi_host    = SPI2_HOST;   // HSPI
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = PIN_TFT_SCLK;
      cfg.pin_mosi    = PIN_TFT_MOSI;
      cfg.pin_miso    = PIN_TFT_MISO;
      cfg.pin_dc      = PIN_TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {   // Panel (ST7796S)
      auto cfg = _panel.config();
      cfg.pin_cs           = PIN_TFT_CS;
      cfg.pin_rst          = PIN_TFT_RST;
      cfg.pin_busy         = -1;
      cfg.panel_width      = TFT_PANEL_WIDTH;
      cfg.panel_height     = TFT_PANEL_HEIGHT;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = (CYD_INVERT_DISPLAY != 0);
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = true;   // touch lives on the same SPI bus
      _panel.config(cfg);
    }
    {   // Backlight (PWM, low freq per cyd.md)
      auto cfg = _light.config();
      cfg.pin_bl      = PIN_TFT_BL;
      cfg.invert      = (BACKLIGHT_ACTIVE_HIGH == 0);
      cfg.freq        = BACKLIGHT_PWM_FREQ;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {   // Touch (XPT2046 on the shared bus)
      auto cfg = _touch.config();
      cfg.x_min          = 300;      // rough defaults; overwritten by calibration
      cfg.x_max          = 3900;
      cfg.y_min          = 200;
      cfg.y_max          = 3700;
      cfg.pin_int        = PIN_TOUCH_IRQ;
      cfg.bus_shared     = true;
      cfg.offset_rotation = 0;
      cfg.spi_host       = SPI2_HOST;
      cfg.freq           = 1000000;
      cfg.pin_sclk       = PIN_TFT_SCLK;
      cfg.pin_mosi       = PIN_TFT_MOSI;
      cfg.pin_miso       = PIN_TFT_MISO;
      cfg.pin_cs         = PIN_TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

// ===========================================================================
#elif defined(BOARD_CYD28_ILI9341)
// ===========================================================================
// ILI9341 240x320 over HSPI; XPT2046 resistive touch on a SEPARATE VSPI bus
// (the 2.8" CYD wires touch to its own SPI, unlike the 4" board).

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341  _panel;
  lgfx::Bus_SPI        _bus;
  lgfx::Light_PWM      _light;
  lgfx::Touch_XPT2046  _touch;

public:
  LGFX() {
    {   // Display SPI bus (HSPI: 12/13/14)
      auto cfg = _bus.config();
      cfg.spi_host    = SPI2_HOST;   // HSPI
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = PIN_TFT_SCLK;
      cfg.pin_mosi    = PIN_TFT_MOSI;
      cfg.pin_miso    = PIN_TFT_MISO;
      cfg.pin_dc      = PIN_TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {   // Panel (ILI9341) — driven LANDSCAPE-NATIVE at MV=0 (see Board.h):
      // panel + memory are 320x240 so rotation 0 keeps MV=0 (the only
      // visually-correct orientation on this mounting) while reporting a 320x240
      // landscape canvas. rgb_order drops the BGR bit for this board's R/B wiring.
      auto cfg = _panel.config();
      cfg.pin_cs           = PIN_TFT_CS;
      cfg.pin_rst          = PIN_TFT_RST;
      cfg.pin_busy         = -1;
      cfg.panel_width      = TFT_PANEL_WIDTH;   // 320
      cfg.panel_height     = TFT_PANEL_HEIGHT;  // 240
      cfg.memory_width     = TFT_PANEL_WIDTH;   // 320
      cfg.memory_height    = TFT_PANEL_HEIGHT;  // 240
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = (CYD_INVERT_DISPLAY != 0);
      cfg.rgb_order        = (CYD_PANEL_RGB_ORDER != 0);  // drop BGR (Board.h)
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = false;  // touch is on its own bus
      _panel.config(cfg);
    }
    {   // Backlight
      auto cfg = _light.config();
      cfg.pin_bl      = PIN_TFT_BL;
      cfg.invert      = (BACKLIGHT_ACTIVE_HIGH == 0);
      cfg.freq        = BACKLIGHT_PWM_FREQ;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {   // Touch (XPT2046 on its own VSPI bus)
      auto cfg = _touch.config();
      cfg.x_min          = 300;      // rough defaults; overwritten by calibration
      cfg.x_max          = 3900;
      cfg.y_min          = 200;
      cfg.y_max          = 3700;
      cfg.pin_int        = PIN_TOUCH_IRQ;
      cfg.bus_shared     = false;
      cfg.offset_rotation = 0;
      cfg.spi_host       = SPI3_HOST; // VSPI — separate from the display bus
      cfg.freq           = 1000000;
      cfg.pin_sclk       = PIN_TOUCH_SCLK;
      cfg.pin_mosi       = PIN_TOUCH_MOSI;
      cfg.pin_miso       = PIN_TOUCH_MISO;
      cfg.pin_cs         = PIN_TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

// ===========================================================================
#elif defined(BOARD_CROWPANEL_S3_5HMI)
// ===========================================================================
// ST7262 800x480 16-bit parallel RGB on ESP32-S3 (framebuffer in PSRAM);
// GT911 capacitive touch over I2C. Backlight is behind an I2C expander and is
// driven by hal/Display (not LovyanGFX) — see Display.cpp.
//
// Bus_RGB/Panel_RGB are NOT pulled in by <LovyanGFX.hpp> — only the lgfx_user
// board presets include the S3 platform headers, so we include them here too.
// (They self-gate on __has_include(<esp_lcd_panel_rgb.h>), which the
// arduino-esp32 3.x / IDF 5 framework provides.)
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_RGB        _bus;
  lgfx::Panel_RGB      _panel;
  lgfx::Touch_GT911    _touch;

public:
  LGFX() {
    {   // Panel geometry (framebuffer in PSRAM)
      auto cfg = _panel.config();
      cfg.memory_width  = TFT_PANEL_WIDTH;
      cfg.memory_height = TFT_PANEL_HEIGHT;
      cfg.panel_width   = TFT_PANEL_WIDTH;
      cfg.panel_height  = TFT_PANEL_HEIGHT;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      _panel.config(cfg);
    }
    {
      auto cfg = _panel.config_detail();
      cfg.use_psram = 1;             // 800x480 RGB565 = 750 KB fb in PSRAM
      _panel.config_detail(cfg);
    }
    {   // 16-bit parallel RGB bus + sync timing
      auto cfg = _bus.config();
      cfg.panel = &_panel;
      cfg.pin_d0  = PIN_RGB_D0;  cfg.pin_d1  = PIN_RGB_D1;
      cfg.pin_d2  = PIN_RGB_D2;  cfg.pin_d3  = PIN_RGB_D3;
      cfg.pin_d4  = PIN_RGB_D4;  cfg.pin_d5  = PIN_RGB_D5;
      cfg.pin_d6  = PIN_RGB_D6;  cfg.pin_d7  = PIN_RGB_D7;
      cfg.pin_d8  = PIN_RGB_D8;  cfg.pin_d9  = PIN_RGB_D9;
      cfg.pin_d10 = PIN_RGB_D10; cfg.pin_d11 = PIN_RGB_D11;
      cfg.pin_d12 = PIN_RGB_D12; cfg.pin_d13 = PIN_RGB_D13;
      cfg.pin_d14 = PIN_RGB_D14; cfg.pin_d15 = PIN_RGB_D15;
      cfg.pin_henable     = PIN_RGB_DE;
      cfg.pin_vsync       = PIN_RGB_VSYNC;
      cfg.pin_hsync       = PIN_RGB_HSYNC;
      cfg.pin_pclk        = PIN_RGB_PCLK;
      cfg.freq_write      = RGB_PCLK_HZ;
      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = RGB_HSYNC_FRONT;
      cfg.hsync_pulse_width = RGB_HSYNC_PULSE;
      cfg.hsync_back_porch  = RGB_HSYNC_BACK;
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = RGB_VSYNC_FRONT;
      cfg.vsync_pulse_width = RGB_VSYNC_PULSE;
      cfg.vsync_back_porch  = RGB_VSYNC_BACK;
      cfg.pclk_active_neg   = 1;
      cfg.de_idle_high      = 0;
      cfg.pclk_idle_high    = 0;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {   // GT911 capacitive touch (I2C). No calibration needed.
      auto cfg = _touch.config();
      cfg.x_min      = 0;
      cfg.x_max      = TFT_PANEL_WIDTH  - 1;
      cfg.y_min      = 0;
      cfg.y_max      = TFT_PANEL_HEIGHT - 1;
      cfg.pin_int    = PIN_TOUCH_INT;   // reset is handled by the I2C expander
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port   = 1;            // keep GT911 off Wire's port-0 (expander)
      cfg.i2c_addr   = I2C_ADDR_GT911;
      cfg.pin_sda    = PIN_I2C_SDA;
      cfg.pin_scl    = PIN_I2C_SCL;
      cfg.freq       = I2C_FREQ_HZ;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

#endif
