#include "MorseBeeper.h"
#include "../services/Settings.h"
#include "ThemeController.h"
#include "../hal/Board.h"
#include <ctype.h>

#ifndef BUZZER_PIN
#define BUZZER_PIN -1
#endif
#ifndef BUZZER_FREQ
#define BUZZER_FREQ 2400          // PWM tone (Hz) for a passive buzzer/speaker
#endif
#ifndef BUZZER_ACTIVE
#define BUZZER_ACTIVE 0           // 0 = PWM tone (passive speaker/buzzer); 1 = digital on/off (active buzzer)
#endif

#if BUZZER_PIN >= 0 && !BUZZER_ACTIVE && ESP_ARDUINO_VERSION_MAJOR < 3
static const int kBuzzerCh = 6;   // core 2.x LEDC channel (the backlight has its own)
#endif

// International Morse: A-Z then 0-9.
static const char* const kLetters[26] = {
  ".-","-...","-.-.","-..",".","..-.","--.","....","..","---","-.-",".-..","--",
  "-.","---",".--.","--.-",".-.","...","-","..-","...-",".--","-..-","-.--","--.." };
static const char* const kDigits[10] = {
  "-----",".----","..---","...--","....-",".....","-....","--...","---..","----." };

static const char* pattern(char c) {
  c = (char)toupper((unsigned char)c);
  if (c >= 'A' && c <= 'Z') return kLetters[c - 'A'];
  if (c >= '0' && c <= '9') return kDigits[c - '0'];
  return nullptr;                 // skip spaces / punctuation
}

void MorseBeeper::begin(Settings* s, ThemeController* theme) {
  _s = s; _theme = theme;
#if BUZZER_PIN >= 0
  #if BUZZER_ACTIVE
    pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW); _attached = true;
  #elif ESP_ARDUINO_VERSION_MAJOR >= 3
    _attached = ledcAttach(BUZZER_PIN, BUZZER_FREQ, 8);            // core 3.x: pin-based LEDC
  #else
    ledcSetup(kBuzzerCh, BUZZER_FREQ, 8); ledcAttachPin(BUZZER_PIN, kBuzzerCh); _attached = true;  // core 2.x
  #endif
  Serial.printf("[morse] buzzer pin=%d active=%d freq=%d attached=%d\n",
                BUZZER_PIN, BUZZER_ACTIVE, BUZZER_FREQ, (int)_attached);
#else
  Serial.println("[morse] no buzzer pin for this board");
#endif
}

void MorseBeeper::toneOn() {
#if BUZZER_PIN >= 0
  if (!_attached) return;
  #if BUZZER_ACTIVE
    digitalWrite(BUZZER_PIN, HIGH);
  #elif ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(BUZZER_PIN, 128);       // 50% duty
  #else
    ledcWrite(kBuzzerCh, 128);
  #endif
#endif
}

void MorseBeeper::toneOff() {
#if BUZZER_PIN >= 0
  if (!_attached) return;
  #if BUZZER_ACTIVE
    digitalWrite(BUZZER_PIN, LOW);
  #elif ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(BUZZER_PIN, 0);
  #else
    ledcWrite(kBuzzerCh, 0);
  #endif
#endif
}

// Lay out the timed on/off segments for one word. Koch sets the element speed
// (dit = 1200/K ms; dah = 3 units; intra-character gap = 1 unit). Farnsworth (F <= K)
// stretches ONLY the inter-character gap so the word plays at the slower effective
// speed while the characters themselves stay crisp at K -- the classic learning aid.
void MorseBeeper::buildSegments(const String& word) {
  _segN = 0; _segI = 0;
  long K = _s ? _s->getInt("audioKochWpm", 18) : 18;        if (K < 5) K = 5; if (K > 40) K = 40;
  long F = _s ? _s->getInt("audioFarnsworthWpm", 12) : 12;  if (F < 5) F = 5; if (F > K) F = K;
  int u  = (int)(1200 / K);                                 // dit (ms) at the character speed
  int fu = u;                                               // Farnsworth spacing unit
  if (F < K) { long sp = 60000L / F - 37200L / K; fu = (int)(sp / 19); if (fu < u) fu = u; }
  auto push = [&](bool on, int ms) { if (_segN < kMaxSeg && ms > 0) _seg[_segN++] = { on, (uint16_t)ms }; };
  bool first = true;
  for (size_t i = 0; i < word.length() && _segN < kMaxSeg - 12; ++i) {
    const char* p = pattern(word[i]);
    if (!p) continue;
    if (!first) push(false, 3 * fu);            // inter-character gap (Farnsworth-stretched)
    first = false;
    for (int j = 0; p[j]; ++j) {
      if (j) push(false, u);                    // intra-character gap
      push(true, p[j] == '-' ? 3 * u : u);      // dah / dit
    }
  }
}

void MorseBeeper::onAlert(const String& title) {
  if (!_s || !_s->getBool("audioEnabled", false)) return;
  if (_theme && _theme->isNight() && !_s->getBool("audioBeepAtNight", false)) return;   // quiet at night
  String t = title; t.trim();
  int sp = t.indexOf(' ');
  String w = (sp > 0) ? t.substring(0, sp) : t; w.trim();   // first word = the call sign (ISS, FALCON...)
  if (w.length()) play(w);
}

void MorseBeeper::play(const String& word) {
  buildSegments(word);
  if (_segN == 0) { _playing = false; return; }
  _segI = 0; _playing = true; _segEnd = millis();           // first tick starts segment 0
  Serial.printf("[morse] beep \"%s\" (%d segs)\n", word.c_str(), _segN);
}

void MorseBeeper::tick(uint32_t now) {
  if (!_playing) return;
  if ((int32_t)(now - _segEnd) < 0) return;                 // current segment still sounding
  if (_segI >= _segN) { toneOff(); _playing = false; return; }
  const Seg& s = _seg[_segI++];
  if (s.on) toneOn(); else toneOff();
  _segEnd = now + s.ms;
}
