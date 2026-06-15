#pragma once
// config.h — BUILD-TIME feature flags only (spec §1: "config.h is build-time
// flags only"). Anything a user might change at runtime (location, enabled
// tabs, units, refresh intervals, focus behaviour) lives in services/Settings,
// NOT here.
//
// Per-board pins and capability flags live in hal/Board.h, selected by the
// BOARD_* define in platformio.ini.

// --- Optional / license-gated subsystems -----------------------------------
// MarScaper Ephemeris is GPLv3 (spec §5); confine it behind this flag so a
// permissive build can drop it. Off until the astro core lands (milestone 2).
#ifndef ENABLE_EPHEM
#define ENABLE_EPHEM 0
#endif

// --- Serial diagnostics ------------------------------------------------------
#ifndef OVERHEAD_LOG_BAUD
#define OVERHEAD_LOG_BAUD 115200
#endif

// Run the astronomy-core self-test at boot (Time/Coords/Sun/SatEngine vs a
// known ISS TLE; prints PASS/FAIL to serial). Cheap; turn off once trusted.
#ifndef ASTRO_SELFTEST
#define ASTRO_SELFTEST 1
#endif
