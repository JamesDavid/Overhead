// =============================================================================
//  Overhead Companion Rotor  —  alt/az pointer driven over ESP-NOW
//  Target: plain ESP32 (WROOM devkit). Arduino-ESP32 core 3.x.
//
//  Receives telemetry_t {az, el, az_rate, el_rate, valid, seq, ...} from Overhead.
//  Azimuth : open-loop steps from limit-switch home + north offset.
//  Elevation: accel-trimmed (MPU6050 gravity vector closes the loop / homes it).
//  Motion  : rate-based — extrapolate target between packets, coast on dropout.
//
//  Milestone 2: the §11 baseline firmware, migrated onto the shared telemetry_t
//  contract (was an inline pointing_t). Otherwise unchanged. config.h / driver
//  abstraction / calibration arrive in later milestones.
// =============================================================================

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <AccelStepper.h>     // Library Manager: "AccelStepper"
#include <Preferences.h>      // NVS-backed calibration store (§8)
#include <math.h>

#include "shared/telemetry.h" // THE wire format (§4) — via -I <monorepo root>
#include "config.h"           // per-build tunables + BYJ/NEMA presets (§5)

// The wire format is fixed-size + packed; guard it so a silent struct-layout drift
// (the classic split-repo failure) fails the BUILD, not a field in the field.
static_assert(sizeof(telemetry_t) == 36, "telemetry_t layout drifted — check shared/telemetry.h");

// Channel-hunt list materialised from config (§5). 1/6/11 first, then the rest.
static const uint8_t  SCAN_CH[]  = SCAN_CH_LIST;
static const uint8_t  N_SCAN_CH  = sizeof(SCAN_CH) / sizeof(SCAN_CH[0]);

// ----------------------------------------------------------------------------
// Steppers — driver abstraction (§6). Each axis is constructed for its config-
// selected driver: unipolar 4-wire (28BYJ/ULN2003) uses AccelStepper::HALF4WIRE
// with the (IN1,IN3,IN2,IN4) sequencing-quirk pin order; STEP/DIR (NEMA + A4988/
// TMC2209) uses AccelStepper::DRIVER with the STEP/DIR pins. AccelStepper handles
// the stepping for both — we never hand-roll it.
#if AZ_DRIVER == DRIVER_STEP_DIR
AccelStepper azM(AccelStepper::DRIVER, AZ_PIN_STEP, AZ_PIN_DIR);
#else
AccelStepper azM(AccelStepper::HALF4WIRE, AZ_PIN_IN1, AZ_PIN_IN3, AZ_PIN_IN2, AZ_PIN_IN4);
#endif
#if EL_DRIVER == DRIVER_STEP_DIR
AccelStepper elM(AccelStepper::DRIVER, EL_PIN_STEP, EL_PIN_DIR);
#else
AccelStepper elM(AccelStepper::HALF4WIRE, EL_PIN_IN1, EL_PIN_IN3, EL_PIN_IN2, EL_PIN_IN4);
#endif

// Runtime calibration state (§7/§8). Starts from the config.h defaults, then NVS
// overrides it on boot; CAL/SETNORTH MEASURE and persist. This is the self-measuring
// gear ratio — the user never enters one by hand. The per-axis direction sign lives
// here too (config `invert`, or flipped when calibration discovers a reversed axis),
// applied uniformly to homing feeds AND step commands (unipolar has no clean pin-level
// direction invert, so a sign is the portable way).
float g_azSpd    = AZ_STEPS_PER_DEG;    // az steps per degree
float g_elSpd    = EL_STEPS_PER_DEG;    // el steps per degree
float g_northOff = NORTH_OFFSET_DEG;    // mechanical-zero (switch) -> true north
int   g_azSign   = (AZ_INVERT ? -1 : 1);
int   g_elSign   = (EL_INVERT ? -1 : 1);

Preferences prefs;                      // NVS namespace "rotor"

// Wire up any STEP/DIR enable pins (A4988/TMC2209 EN is active-low). No-op for unipolar.
void driverBegin() {
#if AZ_DRIVER == DRIVER_STEP_DIR
  if (AZ_PIN_EN >= 0) { azM.setEnablePin(AZ_PIN_EN); azM.setPinsInverted(false, false, true); azM.enableOutputs(); }
#endif
#if EL_DRIVER == DRIVER_STEP_DIR
  if (EL_PIN_EN >= 0) { elM.setEnablePin(EL_PIN_EN); elM.setPinsInverted(false, false, true); elM.enableOutputs(); }
#endif
}

// Pointing state received from Overhead. The full shared contract; the rotor reads the
// pointing fields (az/el/az_rate/el_rate/valid/seq) and ignores the reserved radio fields.
volatile telemetry_t rxPkt   = {0};
volatile uint32_t     rxTimeMs = 0;
volatile bool         haveData = false;

enum State { SCANNING, HOMING_AZ, HOMING_EL, TRACKING, PARK, CALIBRATION };
State state = SCANNING;

uint8_t  scanIdx = 0;
uint32_t chanChangeMs = 0;
uint8_t  lockedChannel = 0;

// ----------------------------------------------------------------------------
//  MPU6050 — raw accel read, no extra libs. Returns pitch (elevation) in deg.
// ----------------------------------------------------------------------------
void mpuInit() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0x00);          // PWR_MGMT_1 = 0 -> wake
  Wire.endTransmission();
}

float readPitchDeg() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);                            // ACCEL_XOUT_H
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);
  int16_t ax = (Wire.read() << 8) | Wire.read();
  int16_t ay = (Wire.read() << 8) | Wire.read();
  int16_t az = (Wire.read() << 8) | Wire.read();
  // pitch about the el axis — adjust axis mapping to how the IMU sits on the el arm
  return atan2f((float)ax, sqrtf((float)ay*ay + (float)az*az)) * 180.0f / PI;
}

// ----------------------------------------------------------------------------
//  ESP-NOW
// ----------------------------------------------------------------------------
// Core 3.x signature. (Core 2.x: void onRecv(const uint8_t* mac, const uint8_t* d, int len))
void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != sizeof(telemetry_t)) return;                       // §4: reject on wrong length
  if (data[0] != TELEM_PROTO_VER) return;                       // §4: reject on proto mismatch (proto_ver is byte 0)
  memcpy((void*)&rxPkt, data, sizeof(telemetry_t));
  rxTimeMs = millis();
  haveData = true;
}

void radioInit() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();                                   // never associate
  esp_wifi_set_channel(SCAN_CH[0], WIFI_SECOND_CHAN_NONE);  // hunt begins here
  if (esp_now_init() != ESP_OK) { Serial.println("esp_now_init failed"); return; }
  esp_now_register_recv_cb(onRecv);
}

// ----------------------------------------------------------------------------
//  Coordinate helpers
// ----------------------------------------------------------------------------
float wrap360(float d){ while(d<0)d+=360; while(d>=360)d-=360; return d; }

long azDegToSteps(float trueAz) {
  float mech = wrap360(trueAz - g_northOff);
  // TODO cable-wrap: if a target crosses the no-go seam, take the long way instead
  return g_azSign * lroundf(mech * g_azSpd);
}
long elDegToSteps(float el) {
  el = constrain(el, EL_MIN_DEG, EL_MAX_DEG);
  return g_elSign * lroundf(el * g_elSpd);
}

// ----------------------------------------------------------------------------
//  Channel hunt
// ----------------------------------------------------------------------------
void setChan(uint8_t ch) {
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  chanChangeMs = millis();
}
void startScan() { scanIdx = 0; setChan(SCAN_CH[0]); state = SCANNING; }

void scan() {
  // a valid packet received since we hopped here == Overhead is on this channel
  if (haveData && rxTimeMs >= chanChangeMs) {
    lockedChannel = SCAN_CH[scanIdx];
    Serial.printf("locked on channel %u\n", lockedChannel);
    state = HOMING_AZ;
    return;
  }
  if (millis() - chanChangeMs > SCAN_DWELL_MS) {        // nothing here, hop on
    scanIdx = (scanIdx + 1) % N_SCAN_CH;
    setChan(SCAN_CH[scanIdx]);
  }
}

// ----------------------------------------------------------------------------
//  Calibration (§7) + NVS persistence (§8)
// ----------------------------------------------------------------------------
// Averaged pitch read (gravity reference) — cuts accelerometer noise during cal.
float readPitchAvg(int n = 12) {
  float s = 0; for (int i = 0; i < n; ++i) { s += readPitchDeg(); delay(5); } return s / n;
}

void nvsLoad() {
  prefs.begin("rotor", true);                     // read-only
  g_azSpd    = prefs.getFloat("azSpd",  g_azSpd);
  g_elSpd    = prefs.getFloat("elSpd",  g_elSpd);
  g_northOff = prefs.getFloat("north",  g_northOff);
  g_azSign   = prefs.getInt  ("azSign", g_azSign);
  g_elSign   = prefs.getInt  ("elSign", g_elSign);
  prefs.end();
}
void nvsSave() {
  prefs.begin("rotor", false);
  prefs.putFloat("azSpd", g_azSpd);   prefs.putFloat("elSpd", g_elSpd);
  prefs.putFloat("north", g_northOff);
  prefs.putInt  ("azSign", g_azSign); prefs.putInt("elSign", g_elSign);
  prefs.end();
}
void nvsReset() {
  prefs.begin("rotor", false); prefs.clear(); prefs.end();
  g_azSpd = AZ_STEPS_PER_DEG; g_elSpd = EL_STEPS_PER_DEG; g_northOff = NORTH_OFFSET_DEG;
  g_azSign = (AZ_INVERT ? -1 : 1); g_elSign = (EL_INVERT ? -1 : 1);
}

void showConfig() {
  Serial.printf("[cfg] az=%.3f steps/deg (sign %d)  el=%.3f steps/deg (sign %d)  north=%.2f deg\n",
                g_azSpd, g_azSign, g_elSpd, g_elSign, g_northOff);
}

// Blocking helpers, used only during operator-driven calibration.
void homeAzBlocking() {
  azM.setSpeed(g_azSign * -HOME_SPEED);
  while (digitalRead(LIMIT_PIN) != LIMIT_ACTIVE) azM.runSpeed();
  azM.setCurrentPosition(0);
}
void jogAxis(AccelStepper& m, int sign, float spd, float deg) {
  m.move(sign * lroundf(deg * spd));
  while (m.distanceToGo() != 0) m.run();
}

// EL steps/deg — automatic + exact: step a block, read the gravity pitch before/after,
// steps_per_deg = |Δsteps| / |Δpitch|. Averaged over a few spans; also learns el direction.
void calEl() {
  Serial.println("[cal] EL: measuring steps/deg against gravity (keep the el axis free)...");
  const int SPANS = 3;
  long block = lroundf(g_elSpd * 20.0f);          // ~20 deg per span using the current estimate
  if (block < 50) block = 50;
  float sum = 0; int n = 0, signVotes = 0;
  for (int s = 0; s < SPANS; ++s) {
    float p0 = readPitchAvg();
    long from = elM.currentPosition();
    elM.move(block); while (elM.distanceToGo() != 0) elM.run();
    delay(300);
    float dp = readPitchAvg() - p0;
    if (fabsf(dp) > 2.0f) {                        // enough travel to trust the ratio
      sum += (float)block / fabsf(dp); n++;
      signVotes += (dp >= 0) ? 1 : -1;            // +steps -> +pitch means sign matches
      Serial.printf("[cal] EL span %d: %ld steps / %.2f deg = %.3f\n", s, block, fabsf(dp), block / fabsf(dp));
    }
    elM.moveTo(from); while (elM.distanceToGo() != 0) elM.run();   // return
    delay(300);
  }
  if (n == 0) { Serial.println("[cal] EL failed — too little pitch change. Is the el axis free / IMU mounted?"); return; }
  g_elSpd = sum / n;
  if (signVotes < 0) { g_elSign = -g_elSign; Serial.println("[cal] EL reads reversed -> flipped el sign"); }
  nvsSave();
  Serial.printf("[cal] EL = %.3f steps/deg (saved)\n", g_elSpd);
}

// Manual AZ cal state (cable-wrap fallback): two MARKs 360 deg apart -> steps/deg (§7).
bool g_azMarked = false;
long g_azMarkPos = 0;

// AZ steps/deg — full-turn auto: home to the switch, jog until the switch triggers again
// (exactly one mechanical revolution), steps/360. Falls back to guided manual (MARK) if
// cable-wrap stops a full turn.
void calAz() {
  Serial.println("[cal] AZ: homing, then one full turn back to the switch...");
  homeAzBlocking();                               // switch active, position = 0
  azM.setSpeed(g_azSign * (AZ_MAX_SPEED * 0.6f));
  bool leftFlag = false;
  long maxSteps = lroundf(g_azSpd * 400.0f);      // safety cap > one revolution
  while (labs(azM.currentPosition()) < maxSteps) {
    azM.runSpeed();
    bool active = (digitalRead(LIMIT_PIN) == LIMIT_ACTIVE);
    if (!leftFlag) { if (!active) leftFlag = true; }         // rolled off the home flag
    else if (active) {                                        // flag again -> full revolution
      long steps = labs(azM.currentPosition());
      g_azSpd = steps / 360.0f; nvsSave();
      Serial.printf("[cal] AZ full turn = %ld steps -> %.3f steps/deg (saved)\n", steps, g_azSpd);
      return;
    }
  }
  Serial.println("[cal] AZ auto-cal: switch never re-triggered (cable-wrap prevents a full turn?).");
  Serial.println("[cal] Manual fallback: jog to a reference mark (AZ+/AZ- <deg>), send MARK,");
  Serial.println("      jog exactly 360 deg back to the SAME mark, send MARK again.");
  g_azMarked = false;
}

void markAz() {
  if (!g_azMarked) {
    g_azMarkPos = azM.currentPosition(); g_azMarked = true;
    Serial.println("[cal] mark 1 set. Jog exactly 360 deg back to the same physical mark, then MARK.");
  } else {
    long steps = labs(azM.currentPosition() - g_azMarkPos);
    g_azMarked = false;
    if (steps < 100) { Serial.println("[cal] marks too close — did you jog a full 360?"); return; }
    g_azSpd = steps / 360.0f; nvsSave();
    Serial.printf("[cal] AZ (manual) = %ld steps / 360 -> %.3f steps/deg (saved)\n", steps, g_azSpd);
  }
}

// SETNORTH: the operator has jogged the pointer to true north; store the current
// mechanical azimuth (deg from the switch home) as the offset.
void setNorth() {
  float mech = (float)azM.currentPosition() / (g_azSign * g_azSpd);
  g_northOff = wrap360(mech); nvsSave();
  Serial.printf("[cal] NORTH offset = %.2f deg (saved)\n", g_northOff);
}

// USB serial command menu (§7). The CAL routines block (deliberate, operator-driven),
// then hand back to hunting Overhead's channel.
void serviceSerial() {
  if (!Serial.available()) return;
  String c = Serial.readStringUntil('\n'); c.trim(); c.toUpperCase();
  if      (c == "CAL AZ")   { state = CALIBRATION; calAz(); startScan(); }
  else if (c == "CAL EL")   { state = CALIBRATION; calEl(); startScan(); }
  else if (c == "SETNORTH") { setNorth(); }
  else if (c == "HOME")     { startScan(); state = HOMING_AZ; }
  else if (c == "SHOW")     { showConfig(); }
  else if (c == "RESET")    { nvsReset(); Serial.println("[cfg] NVS cleared -> config.h defaults"); showConfig(); }
  else if (c == "MARK")     { markAz(); }
  else if (c.startsWith("AZ+")) jogAxis(azM, g_azSign, g_azSpd,  c.substring(3).toFloat());
  else if (c.startsWith("AZ-")) jogAxis(azM, g_azSign, g_azSpd, -c.substring(3).toFloat());
  else if (c.startsWith("EL+")) jogAxis(elM, g_elSign, g_elSpd,  c.substring(3).toFloat());
  else if (c.startsWith("EL-")) jogAxis(elM, g_elSign, g_elSpd, -c.substring(3).toFloat());
  else if (c.length())
    Serial.println("cmds: CAL AZ | CAL EL | SETNORTH | HOME | SHOW | RESET | AZ+/AZ-/EL+/EL- <deg> | MARK");
}

// ----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(LIMIT_PIN, INPUT_PULLUP);
  mpuInit();
  nvsLoad();                     // calibration over config.h defaults (§8)
  azM.setMaxSpeed(AZ_MAX_SPEED); azM.setAcceleration(AZ_ACCEL);
  elM.setMaxSpeed(EL_MAX_SPEED); elM.setAcceleration(EL_ACCEL);
  driverBegin();                 // STEP/DIR enable pins (§6); no-op for unipolar
  showConfig();
  // Optional (§7): hold the limit switch at boot -> a calibration prompt. The serial menu
  // is available in any state, so this is just a hint; motion still proceeds normally.
  if (digitalRead(LIMIT_PIN) == LIMIT_ACTIVE)
    Serial.println("[boot] limit held -> send CAL AZ | CAL EL | SETNORTH to calibrate.");
  radioInit();
  startScan();           // find Overhead's channel before doing anything
}

// --- homing: az to the switch, el to level (accel = 0) ----------------------
void homeAz() {
  azM.setSpeed(g_azSign * -HOME_SPEED);      // drive toward the switch (sign flips with invert)
  if (digitalRead(LIMIT_PIN) == LIMIT_ACTIVE) {
    azM.setCurrentPosition(0);               // mechanical zero
    state = HOMING_EL;
    return;
  }
  azM.runSpeed();
}
void homeEl() {
  float pitch = readPitchDeg();
  if (fabsf(pitch) < 0.5f) {                 // level == elevation 0
    elM.setCurrentPosition(0);
    state = TRACKING;
    return;
  }
  elM.setSpeed(g_elSign * (pitch > 0 ? -HOME_SPEED : HOME_SPEED));
  elM.runSpeed();
}

// --- tracking: extrapolate target from last packet + rate ------------------
void track() {
  telemetry_t p;
  noInterrupts();
  memcpy(&p, (const void*)&rxPkt, sizeof(p));
  uint32_t t = rxTimeMs;
  interrupts();

  if (!haveData || (millis() - t) > PACKET_TIMEOUT_MS) { state = PARK; return; }
  if (!p.valid) { state = PARK; return; }

  float dt = (millis() - t) / 1000.0f;
  float azCmd = wrap360(p.az + p.az_rate * dt);       // coast at rate between updates
  float elCmd = p.el + p.el_rate * dt;

  // elevation closed-loop trim against gravity (kills BYJ backlash on el)
  if (EL_TRIM_GAIN > 0) {
    float err = elCmd - readPitchDeg();
    if (fabsf(err) > EL_TRIM_DEADBAND_DEG) elCmd += EL_TRIM_GAIN * err;
  }

  azM.moveTo(azDegToSteps(azCmd));
  elM.moveTo(elDegToSteps(elCmd));
  azM.run(); elM.run();
}

void park() {
  if (millis() - rxTimeMs > RESCAN_MS) { startScan(); return; }  // lost -> re-hunt
  azM.moveTo(0);
  elM.moveTo(elDegToSteps(EL_MIN_DEG));
  azM.run(); elM.run();
  // when a fresh valid packet shows up, resume tracking
  if (haveData && rxPkt.valid && (millis() - rxTimeMs) < PACKET_TIMEOUT_MS) state = TRACKING;
}

void loop() {
  serviceSerial();               // USB calibration menu (§7), available in any state
  switch (state) {
    case SCANNING:  scan();   break;
    case HOMING_AZ: homeAz(); break;
    case HOMING_EL: homeEl(); break;
    case TRACKING:  track();  break;
    case PARK:      park();   break;
    case CALIBRATION: break;   // CAL routines run (blocking) from serviceSerial()
    default: break;
  }
}
