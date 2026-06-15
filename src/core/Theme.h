#pragma once
#include <stdint.h>

// core/Theme — runtime palette every widget reads (spec §7.9). Pages must never
// hardcode colours, so day/night (and red dark-adapt) is a global, instant swap.
// Milestone 7 adds the ThemeController that flips this on Sun altitude; for now
// it's a single dark default.

using Color = uint16_t;  // RGB565

struct Theme {
  Color bg;
  Color fg;
  Color dim;
  Color accent;
  Color grid;
  Color warn;
  Color ok;
};

// RGB565 helper (constexpr so palettes are compile-time).
constexpr Color rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (Color)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace themes {
  constexpr Theme dark = {
    /*bg*/     rgb565(8, 10, 16),
    /*fg*/     rgb565(230, 232, 240),
    /*dim*/    rgb565(120, 128, 140),
    /*accent*/ rgb565(80, 170, 255),
    /*grid*/   rgb565(40, 48, 60),
    /*warn*/   rgb565(255, 170, 40),
    /*ok*/     rgb565(80, 220, 120),
  };
  // Red dark-adapt palette for night visual/ham ops (offered in milestone 7).
  constexpr Theme redNight = {
    /*bg*/     rgb565(0, 0, 0),
    /*fg*/     rgb565(255, 60, 40),
    /*dim*/    rgb565(120, 24, 16),
    /*accent*/ rgb565(255, 100, 60),
    /*grid*/   rgb565(60, 12, 8),
    /*warn*/   rgb565(255, 140, 60),
    /*ok*/     rgb565(220, 80, 50),
  };
}

// The active palette. Mutable global by design (widgets read it every frame).
extern Theme gTheme;

// Right-pad with spaces so opaque-background text overwrites a previous (longer)
// string in place — no fillRect clear, hence no flicker (spec §4 dirty redraws).
#include <Arduino.h>
inline String padRight(String s, uint16_t width) {
  while (s.length() < width) s += ' ';
  return s;
}
