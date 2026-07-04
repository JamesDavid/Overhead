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

// Per-axis direction sign from config `invert`. Applied uniformly to homing feeds
// AND step commands so a flipped axis behaves the same on unipolar (which has no
// clean pin-level direction invert) and STEP/DIR.
static const int AZ_SIGN = AZ_INVERT ? -1 : 1;
static const int EL_SIGN = EL_INVERT ? -1 : 1;

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

enum State { SCANNING, HOMING_AZ, HOMING_EL, TRACKING, PARK };
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
  float mech = wrap360(trueAz - NORTH_OFFSET_DEG);
  // TODO cable-wrap: if a target crosses the no-go seam, take the long way instead
  return AZ_SIGN * lroundf(mech * AZ_STEPS_PER_DEG);
}
long elDegToSteps(float el) {
  el = constrain(el, EL_MIN_DEG, EL_MAX_DEG);
  return EL_SIGN * lroundf(el * EL_STEPS_PER_DEG);
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
void setup() {
  Serial.begin(115200);
  pinMode(LIMIT_PIN, INPUT_PULLUP);
  mpuInit();
  azM.setMaxSpeed(AZ_MAX_SPEED); azM.setAcceleration(AZ_ACCEL);
  elM.setMaxSpeed(EL_MAX_SPEED); elM.setAcceleration(EL_ACCEL);
  driverBegin();               // STEP/DIR enable pins (§6); no-op for unipolar
  radioInit();
  startScan();           // find Overhead's channel before doing anything
}

// --- homing: az to the switch, el to level (accel = 0) ----------------------
void homeAz() {
  azM.setSpeed(AZ_SIGN * -HOME_SPEED);       // drive toward the switch (sign flips with invert)
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
  elM.setSpeed(EL_SIGN * (pitch > 0 ? -HOME_SPEED : HOME_SPEED));
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
  switch (state) {
    case SCANNING:  scan();   break;
    case HOMING_AZ: homeAz(); break;
    case HOMING_EL: homeEl(); break;
    case TRACKING:  track();  break;
    case PARK:      park();   break;
    default: break;
  }
}
