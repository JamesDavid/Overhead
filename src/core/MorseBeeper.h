#pragma once
#include <Arduino.h>

class Settings;
class ThemeController;

// core/MorseBeeper — when a Director alert fires, beep its first word (the call
// sign: ISS, FALCON, SO-50...) in Morse on the board buzzer/speaker. Koch (the
// per-character element speed) and Farnsworth (the slower effective speed, which
// stretches only the inter-character gaps) WPM are configurable; defaults 18/12.
// Suppressed in the night theme tier unless explicitly enabled. Non-blocking:
// onAlert() enqueues a timed on/off segment list, tick() plays it out.
//
// Audio is a single GPIO square-wave tone via LEDC PWM, so the same path drives
// the CYD's GPIO26 speaker and a passive buzzer on a spare CrowPanel pin. The pin
// is BUZZER_PIN (hal/Board.h); BUZZER_PIN < 0 disables audio at compile time.
class MorseBeeper {
public:
  void begin(Settings* s, ThemeController* theme);
  void onAlert(const String& title);   // gate (enabled + night) then beep the first word
  void play(const String& word);       // beep a word unconditionally (serial test / forced)
  void tick(uint32_t nowMs);           // drive the non-blocking playback
  bool playing() const { return _playing; }

private:
  void toneOn();
  void toneOff();
  void buildSegments(const String& word);

  struct Seg { bool on; uint16_t ms; };
  static constexpr int kMaxSeg = 256;       // ~12 chars of dits/dahs/gaps; plenty for a call sign
  int      _toneHz = 650;                   // PWM tone freq (settings audioToneHz); ignored by on/off buzzers
  Seg      _seg[kMaxSeg];
  int      _segN = 0, _segI = 0;
  uint32_t _segEnd = 0;
  bool     _playing = false;
  bool     _attached = false;

  Settings*        _s = nullptr;
  ThemeController* _theme = nullptr;
};
