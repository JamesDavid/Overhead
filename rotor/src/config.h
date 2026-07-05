#pragma once
// =============================================================================
//  Overhead Rotor — per-build configuration (spec §5)
//
//  Everything build-specific lives here. Keep it small: downstream, each axis
//  reduces to a steps_per_degree, and calibration (§7) MEASURES that at runtime
//  and stores it in NVS (§8) — so you never have to work out a gear ratio.
//
//  The motor/driver preset is chosen by the BUILD TARGET (rotor/platformio.ini):
//    env:byj  -> default             : 28BYJ-48 + ULN2003 (unipolar 4-wire)
//    env:nema -> -D ROTOR_PRESET_NEMA : NEMA 17 + A4988/TMC2209 (STEP/DIR)
//  Both are real targets — build both, flash either from the web flasher dropdown.
//  If your wiring differs from a preset, edit its block and rebuild that env.
// =============================================================================

// Driver kinds (§6). Applied to the AccelStepper construction in the driver layer.
#define DRIVER_UNIPOLAR_4WIRE 0   // 28BYJ-48 / ULN2003  -> AccelStepper::HALF4WIRE
#define DRIVER_STEP_DIR       1   // NEMA 17 + A4988/TMC2209 -> AccelStepper::DRIVER

#if defined(ROTOR_PRESET_NEMA)
// ═══════════════════════════════════════════════════════════════════════════
//  PRESET — NEMA 17 + A4988 / TMC2209   (env:nema)
//  Needs its OWN 12–24 V motor PSU. Microstepping multiplies steps_per_deg, but
//  calibration measures the real value — the numbers below are only a sane default.
// ═══════════════════════════════════════════════════════════════════════════
  #define AZ_DRIVER    DRIVER_STEP_DIR
  #define AZ_PIN_STEP  26
  #define AZ_PIN_DIR   25
  #define AZ_PIN_EN    33           // optional enable (active-low); -1 if hard-tied
  #define AZ_PIN_IN1   -1           // (unipolar coil pins unused for STEP/DIR)
  #define AZ_PIN_IN2   -1
  #define AZ_PIN_IN3   -1
  #define AZ_PIN_IN4   -1
  #define AZ_STEPS_PER_DEG  (200.0f * 16.0f / 360.0f)   // 1.8° motor @16x microstep; calibration overwrites
  #define AZ_MIN_DEG    0.0f
  #define AZ_MAX_DEG    360.0f
  #define AZ_MAX_SPEED  4000.0f     // NEMA + driver take far higher step rates than a BYJ
  #define AZ_ACCEL      8000.0f
  #define AZ_INVERT     0           // 1 = flip direction

  #define EL_DRIVER    DRIVER_STEP_DIR
  #define EL_PIN_STEP  17
  #define EL_PIN_DIR   16
  #define EL_PIN_EN    4
  #define EL_PIN_IN1   -1
  #define EL_PIN_IN2   -1
  #define EL_PIN_IN3   -1
  #define EL_PIN_IN4   -1
  #define EL_STEPS_PER_DEG  (200.0f * 16.0f / 360.0f)
  #define EL_MIN_DEG    0.0f
  #define EL_MAX_DEG    90.0f
  #define EL_MAX_SPEED  4000.0f
  #define EL_ACCEL      8000.0f
  #define EL_INVERT     0

#else
// ═══════════════════════════════════════════════════════════════════════════
//  PRESET — 28BYJ-48 + ULN2003   (env:byj, DEFAULT)
// ═══════════════════════════════════════════════════════════════════════════
  #define AZ_DRIVER    DRIVER_UNIPOLAR_4WIRE
  // Unipolar coil pins. The (IN1,IN3,IN2,IN4) sequencing quirk (the reason it
  // otherwise "just buzzes") is applied in the driver construction (§6), not here.
  #define AZ_PIN_IN1   32
  #define AZ_PIN_IN2   33
  #define AZ_PIN_IN3   25
  #define AZ_PIN_IN4   26
  #define AZ_PIN_STEP  -1           // (STEP/DIR unused for a unipolar axis)
  #define AZ_PIN_DIR   -1
  #define AZ_PIN_EN    -1
  #define AZ_STEPS_PER_DEG  (4096.0f / 360.0f)   // 28BYJ half-step / 360 (~11.378); calibration overwrites
  #define AZ_MIN_DEG    0.0f
  #define AZ_MAX_DEG    360.0f
  #define AZ_MAX_SPEED  700.0f      // 28BYJ stalls past ~900-1000; keep margin
  #define AZ_ACCEL      1200.0f
  #define AZ_INVERT     0

  #define EL_DRIVER    DRIVER_UNIPOLAR_4WIRE
  #define EL_PIN_IN1   27
  #define EL_PIN_IN2   14
  #define EL_PIN_IN3   12           // strapping pin — fine as output once booted
  #define EL_PIN_IN4   13
  #define EL_PIN_STEP  -1
  #define EL_PIN_DIR   -1
  #define EL_PIN_EN    -1
  #define EL_STEPS_PER_DEG  (4096.0f / 360.0f)   // change if the el axis has its own reduction; calibration overwrites
  #define EL_MIN_DEG    0.0f
  #define EL_MAX_DEG    90.0f
  #define EL_MAX_SPEED  700.0f
  #define EL_ACCEL      1200.0f
  #define EL_INVERT     0
#endif

// ── Globals (build-agnostic; shared by both presets) ────────────────────────
//  Limit / home switches. DIY rotors vary — fit only what your build has; every
//  pin below is OPTIONAL except the az home switch. Wire each switch to GND and it
//  reads active-LOW with INPUT_PULLUP (flip the *_ACTIVE macro for normally-closed).
//  -1 = not fitted. Two roles:
//    HOME switch — sets an axis zero during homing.
//    END-STOP    — safety limit: the axis won't drive PAST an active one (moving
//                  away is always allowed). Guards cable-wrap (az) / over-travel (el).
//  NOTE: GPIO34-39 are input-only and have NO internal pullup — a switch there needs
//  an external ~10k to 3V3 (fine for the az home switch, which idles open here).

// Az HOME switch — REQUIRED. Mechanical az zero + the reference for az auto-cal.
#define AZ_LIMIT_PIN     34       // input-only pin is ideal for a switch (saves an output GPIO)
#define AZ_LIMIT_ACTIVE  LOW      // switch to GND

// El HOME switch — OPTIONAL. If fitted, el homes to it (horizon = el 0). If -1, el
// homes off the accelerometer (gravity). Either way the accel still trims el in flight.
#define EL_LIMIT_PIN     -1       // -1 = gravity homing (default); else a switch GPIO
#define EL_LIMIT_ACTIVE  LOW      // switch to GND, INPUT_PULLUP (used only when EL_LIMIT_PIN >= 0)

// Travel END-STOPS — OPTIONAL. Fit at the motion extremes; the axis will not step
// further INTO an active stop. -1 = not fitted (the default: no end-stops).
#define AZ_CCW_LIMIT_PIN -1       // az min end (counter-clockwise travel stop)
#define AZ_CW_LIMIT_PIN  -1       // az max end (clockwise travel stop)
#define EL_MIN_LIMIT_PIN -1       // el min end (below the horizon)
#define EL_MAX_LIMIT_PIN -1       // el max end (past the zenith)
#define ENDSTOP_ACTIVE   LOW      // polarity shared by the four end-stops (switch to GND)

// IMU (MPU6050, 6-DOF) — el reference (gravity): el homing (when no el switch) and el
// trim while tracking. Only the ACCELEROMETER is used; a 9-DOF's magnetometer is not
// read (az heading comes from the home switch + step count + SETNORTH, not a compass).
#define I2C_SDA  21
#define I2C_SCL  22
#define MPU_ADDR 0x68

// North offset: mechanical zero (switch) -> true north. Compile-time default;
// SETNORTH (§7) measures and stores the real value to NVS (§8).
#define NORTH_OFFSET_DEG 0.0f

// Elevation closed-loop accel trim (nulls BYJ backlash against gravity)
#define EL_TRIM_GAIN         0.25f   // 0 = off
#define EL_TRIM_DEADBAND_DEG 0.4f

// Azimuth backlash compensation (steps). On a direction reversal the axis is driven
// this many extra steps so the gear train always loads the same flank — the printed-gear
// pan/tilt heads like thing:314027 need it (az has no gravity reference; el uses the trim
// above instead). 0 = off (default); tune by jogging az back and forth and watching for slack.
#define AZ_BACKLASH_STEPS 0

// Homing feed rate (steps/s)
#define HOME_SPEED 350.0f

// ESP-NOW channel hunt + staleness (§9). 1/6/11 first, then the rest.
#define SCAN_CH_LIST      {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13}
#define SCAN_DWELL_MS     250     // listen this long per channel
#define RESCAN_MS         8000    // signal lost this long -> hunt again
#define PACKET_TIMEOUT_MS 2500    // no data this long -> park

// Always-on setup AP (browser config UI). Open network by default; the web UI
// coexists with ESP-NOW on the same radio channel.
#define AP_SSID  "Rotor-setup"
#define AP_PASS  ""              // "" = open; else 8+ chars for WPA2
