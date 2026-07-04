#pragma once
#include <stdint.h>

// =============================================================================
//  Overhead telemetry contract — THE wire format (spec §4).
//  One packet, broadcast by Overhead, consumed by every node. The rotor uses the
//  pointing fields and ignores the radio fields; the radio fields exist now so the
//  packet shape never has to change when the deferred Doppler node arrives (§10.3).
//
//  DEFERRED (§10.2): this header is the single source of truth for the wire format.
//  Where shared/ ultimately lands relative to the Overhead dashboard sketch, and the
//  Overhead sender that emits telemetry_t at ~2 Hz, are integration work — not this
//  task. Both sides #include this one file so the definition can never drift.
// =============================================================================

#define TELEM_PROTO_VER 1

typedef struct __attribute__((packed)) {
  uint8_t  proto_ver;      // = TELEM_PROTO_VER; receiver rejects mismatch
  uint8_t  valid;          // 1 = target active/above horizon, 0 = park/idle heartbeat
  uint8_t  target_id;      // opaque id of the tracked object (0 = none)
  uint8_t  _pad;

  uint32_t seq;            // monotonic; heartbeat + staleness detection

  // --- pointing (rotor consumes) ---
  float    az;             // deg, 0..360, true-north referenced
  float    el;             // deg, horizon = 0
  float    az_rate;        // deg/s, signed
  float    el_rate;        // deg/s, signed

  // --- radio (reserved for the deferred Doppler node; rotor ignores) ---
  uint32_t base_freq_hz;   // nominal freq (Hz); uint32 covers to 4.29 GHz
  int32_t  doppler_hz;     // signed Doppler offset (Hz)
  float    range_rate_kms; // km/s; lets the radio node recompute Doppler for its own freq
} telemetry_t;
