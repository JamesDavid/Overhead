#include "Touch.h"
#include "Display.h"
#include "Board.h"
#include <Arduino.h>

#if CAP_TOUCH_NEEDS_CAL
  #include <LittleFS.h>
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
  if (!display.gfx().getTouch(&x, &y)) return false;
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
