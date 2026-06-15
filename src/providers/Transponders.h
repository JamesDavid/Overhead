#pragma once
#include <Arduino.h>

// providers/Transponders — tiny static transponder table for live Doppler on
// the common FM birds (spec §6 Satellites).
//
// NOTE (spec §6): the active-AMSAT list goes stale — this is a small illustrative
// seed for the Doppler feature, NOT an authoritative list. Sourcing the live set
// (AMSAT status page) + uplink/mode/band detail is a follow-up James confirms.
struct Transponder {
  const char* match;     // matched as a prefix of the TLE name
  uint32_t    downHz;    // downlink (what you receive)
  uint32_t    upHz;      // uplink
  const char* mode;      // FM | SSB | APRS ...
};

// Frequencies in Hz. Verify before operating.
static const Transponder kTransponders[] = {
  { "ISS",    145800000, 145990000, "FM/APRS" },  // voice down / packet up
  { "SO-50",  436795000, 145850000, "FM" },
  { "AO-91",  145960000, 435250000, "FM" },
  { "PO-101", 145900000, 437500000, "FM" },
  { "AO-27",  436795000, 145850000, "FM" },
};

inline const Transponder* findTransponder(const String& name) {
  for (auto& t : kTransponders)
    if (name.startsWith(t.match)) return &t;
  return nullptr;
}
