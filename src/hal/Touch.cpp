#include "Touch.h"
#include "Display.h"
#include "Board.h"
#include <Arduino.h>

#if CAP_TOUCH_NEEDS_CAL
  #include <LittleFS.h>
#endif

#if defined(BOARD_CROWPANEL_S3_5HMI)
  #include <Wire.h>
// Read the GT911 over Wire (I2C port 0) — the SAME controller/pins as the backlight expander.
// LovyanGFX's Touch_GT911 wanted its own port (1) on the same pins, which the two controllers
// fight over (touch ends up dead). cyd-radio reads it over Wire directly; we do the same.
static bool gt911ReadWire(int16_t& x, int16_t& y) {
  auto rd = [](uint16_t reg, uint8_t* buf, uint8_t n) -> bool {
    Wire.beginTransmission((uint8_t)I2C_ADDR_GT911);
    Wire.write((uint8_t)(reg >> 8)); Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    uint8_t got = Wire.requestFrom((int)I2C_ADDR_GT911, (int)n);
    for (uint8_t i = 0; i < n && Wire.available(); ++i) buf[i] = Wire.read();
    return got == n;
  };
  uint8_t status = 0;
  if (!rd(0x814E, &status, 1)) return false;     // 0x814E: bit7 = ready, bits0-3 = #points
  if ((status & 0x80) == 0) return false;
  bool got = false;
  if ((status & 0x0F) > 0) {
    uint8_t p[4] = {0};
    if (rd(0x8150, p, 4)) { x = (int16_t)(p[0] | (p[1] << 8)); y = (int16_t)(p[2] | (p[3] << 8)); got = true; }
  }
  Wire.beginTransmission((uint8_t)I2C_ADDR_GT911);   // ACK status so the IC preps the next sample
  Wire.write(0x81); Wire.write(0x4E); Wire.write((uint8_t)0x00);
  Wire.endTransmission();
  return got;
}
#endif

bool Touch::begin(Display& display) {
#if CAP_TOUCH_NEEDS_CAL
  uint16_t cal[8];
  if (loadCalibration(cal)) {
    display.gfx().setTouchCalibrate(cal);
    Serial.println("[touch] loaded saved calibration");
    return true;
  }
  Serial.println("[touch] no saved calibration — running first-boot routine");
  return calibrate(display);
#else
  // Capacitive (GT911): factory-calibrated, nothing to do.
  Serial.println("[touch] capacitive — no calibration required");
  return true;
#endif
}

bool Touch::calibrate(Display& display) {
#if CAP_TOUCH_NEEDS_CAL
  auto& lcd = display.gfx();
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.drawString("Touch the corner targets", lcd.width() / 2, lcd.height() / 2);

  uint16_t cal[8];
  // Draws a crosshair in each corner and waits for a firm press at each.
  lcd.calibrateTouch(cal, TFT_GREEN, TFT_BLACK, std::max(lcd.width(), lcd.height()) >> 6);
  saveCalibration(cal);

  Serial.print("[touch] calibrated: ");
  for (int i = 0; i < 8; ++i) Serial.printf("%u ", cal[i]);
  Serial.println();
  return true;
#else
  (void)display;
  return true;   // no-op for capacitive panels
#endif
}

bool Touch::read(Display& display, int16_t& x, int16_t& y) {
#if defined(BOARD_CROWPANEL_S3_5HMI)
  if (!gt911ReadWire(x, y)) return false;        // GT911 over Wire (shares the expander's I2C bus)
  (void)display;
#else
  if (!display.getTouch(&x, &y)) return false;   // via Display (gfx() may be an off-screen canvas)
#endif
  // Board-specific correction for panels whose rotation flip throws off the
  // calibrated touch axes (see Board.h).
#if defined(TOUCH_INVERT_X) && TOUCH_INVERT_X
  x = display.width()  - 1 - x;
#endif
#if defined(TOUCH_INVERT_Y) && TOUCH_INVERT_Y
  y = display.height() - 1 - y;
#endif
  return true;
}

#if CAP_TOUCH_NEEDS_CAL
// Calibration depends on the display rotation, so the saved blob is tagged with
// the rotation it was made at. Changing DISPLAY_DEFAULT_ROTATION therefore
// invalidates the old calibration and triggers a fresh first-boot routine.
bool Touch::loadCalibration(uint16_t out[8]) {
  File f = LittleFS.open(kCalPath, "r");
  if (!f) return false;
  uint16_t rot = 0xFFFF;
  f.read(reinterpret_cast<uint8_t*>(&rot), sizeof(rot));
  size_t n = f.read(reinterpret_cast<uint8_t*>(out), sizeof(uint16_t) * 8);
  f.close();
  if (rot != (uint16_t)DISPLAY_DEFAULT_ROTATION) {
    Serial.println("[touch] saved calibration is for a different rotation — recalibrating");
    return false;
  }
  return n == sizeof(uint16_t) * 8;
}

void Touch::saveCalibration(const uint16_t data[8]) {
  File f = LittleFS.open(kCalPath, "w");
  if (!f) { Serial.println("[touch] WARN: could not persist calibration"); return; }
  uint16_t rot = (uint16_t)DISPLAY_DEFAULT_ROTATION;
  f.write(reinterpret_cast<const uint8_t*>(&rot), sizeof(rot));
  f.write(reinterpret_cast<const uint8_t*>(data), sizeof(uint16_t) * 8);
  f.close();
}
#endif
