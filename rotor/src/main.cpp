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
#include <WebServer.h>        // core web server for the always-on setup AP
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
//  Runtime hardware config (web-configurable, NVS-backed). config.h supplies the
//  factory DEFAULTS (the BYJ preset ships); the always-on setup AP overrides any
//  field and persists it. Making the driver + pins runtime is what lets ONE
//  firmware cover both motor types — the steppers are built in setup() from cfg,
//  not selected at compile time.
// ----------------------------------------------------------------------------
struct RotorCfg {
  uint8_t ver;                 // blob version (NVS migration guard)
  uint8_t azDriver, elDriver;  // DRIVER_UNIPOLAR_4WIRE | DRIVER_STEP_DIR
  // Per-axis pins. Unipolar: [IN1,IN2,IN3,IN4]. STEP/DIR: [STEP,DIR,EN]. -1 = unused.
  int8_t  azPins[4], elPins[4];
  // Switches (-1 = not fitted). Home = axis zero; end-stops = travel-limit safety.
  int8_t  azHomePin, elHomePin;
  int8_t  azCwPin, azCcwPin, elMinPin, elMaxPin;
  uint8_t homeActive, endstopActive;   // 0 = active-LOW, 1 = active-HIGH
  // Radio (used from milestone 2 on): fixed ESP-NOW channel (0 = auto-hunt) + target.
  uint8_t channel, targetId;
  uint16_t azBacklash;                 // az backlash-comp steps on a reversal (0 = off)
};
RotorCfg cfg;

// Seed cfg from the config.h factory defaults (BYJ preset unless built with the flag).
void cfgDefaults() {
  cfg.ver = 2;
  cfg.azDriver = AZ_DRIVER;  cfg.elDriver = EL_DRIVER;
#if AZ_DRIVER == DRIVER_STEP_DIR
  cfg.azPins[0]=AZ_PIN_STEP; cfg.azPins[1]=AZ_PIN_DIR; cfg.azPins[2]=AZ_PIN_EN; cfg.azPins[3]=-1;
#else
  cfg.azPins[0]=AZ_PIN_IN1;  cfg.azPins[1]=AZ_PIN_IN2; cfg.azPins[2]=AZ_PIN_IN3; cfg.azPins[3]=AZ_PIN_IN4;
#endif
#if EL_DRIVER == DRIVER_STEP_DIR
  cfg.elPins[0]=EL_PIN_STEP; cfg.elPins[1]=EL_PIN_DIR; cfg.elPins[2]=EL_PIN_EN; cfg.elPins[3]=-1;
#else
  cfg.elPins[0]=EL_PIN_IN1;  cfg.elPins[1]=EL_PIN_IN2; cfg.elPins[2]=EL_PIN_IN3; cfg.elPins[3]=EL_PIN_IN4;
#endif
  cfg.azHomePin=AZ_LIMIT_PIN; cfg.elHomePin=EL_LIMIT_PIN;
  cfg.azCwPin=AZ_CW_LIMIT_PIN; cfg.azCcwPin=AZ_CCW_LIMIT_PIN;
  cfg.elMinPin=EL_MIN_LIMIT_PIN; cfg.elMaxPin=EL_MAX_LIMIT_PIN;
  cfg.homeActive    = (AZ_LIMIT_ACTIVE == HIGH) ? 1 : 0;
  cfg.endstopActive = (ENDSTOP_ACTIVE  == HIGH) ? 1 : 0;
  cfg.channel = 0;  cfg.targetId = 0;
  cfg.azBacklash = AZ_BACKLASH_STEPS;
}
inline int homeLvl()    { return cfg.homeActive    ? HIGH : LOW; }
inline int endstopLvl() { return cfg.endstopActive ? HIGH : LOW; }

// Steppers are constructed at runtime from cfg (§6 driver abstraction). Unipolar
// 4-wire (28BYJ/ULN2003) keeps the (IN1,IN3,IN2,IN4) sequencing-quirk order; STEP/DIR
// (NEMA + A4988/TMC2209) uses STEP,DIR. Pointer + alias so call sites stay `azM.foo()`.
AccelStepper* g_azM = nullptr;
AccelStepper* g_elM = nullptr;
#define azM (*g_azM)
#define elM (*g_elM)
void buildSteppers() {
  if (cfg.azDriver == DRIVER_STEP_DIR)
    g_azM = new AccelStepper(AccelStepper::DRIVER, cfg.azPins[0], cfg.azPins[1]);
  else
    g_azM = new AccelStepper(AccelStepper::HALF4WIRE, cfg.azPins[0], cfg.azPins[2], cfg.azPins[1], cfg.azPins[3]);
  if (cfg.elDriver == DRIVER_STEP_DIR)
    g_elM = new AccelStepper(AccelStepper::DRIVER, cfg.elPins[0], cfg.elPins[1]);
  else
    g_elM = new AccelStepper(AccelStepper::HALF4WIRE, cfg.elPins[0], cfg.elPins[2], cfg.elPins[1], cfg.elPins[3]);
}

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
  if (cfg.azDriver == DRIVER_STEP_DIR && cfg.azPins[2] >= 0) {
    azM.setEnablePin(cfg.azPins[2]); azM.setPinsInverted(false, false, true); azM.enableOutputs();
  }
  if (cfg.elDriver == DRIVER_STEP_DIR && cfg.elPins[2] >= 0) {
    elM.setEnablePin(cfg.elPins[2]); elM.setPinsInverted(false, false, true); elM.enableOutputs();
  }
}

// --- optional travel end-stops (config-gated) -------------------------------
// True if an active end-stop forbids stepping the axis in step-direction `dir` (+1/-1).
// Unfitted pins (-1) short-circuit to never-block, so the default build (all -1) keeps
// its exact prior motion. Moving AWAY from a stop is always allowed.
static inline bool azStopHit(int dir) {
  if (dir > 0 && cfg.azCwPin  >= 0 && digitalRead(cfg.azCwPin)  == endstopLvl()) return true;
  if (dir < 0 && cfg.azCcwPin >= 0 && digitalRead(cfg.azCcwPin) == endstopLvl()) return true;
  return false;
}
static inline bool elStopHit(int dir) {
  if (dir > 0 && cfg.elMaxPin >= 0 && digitalRead(cfg.elMaxPin) == endstopLvl()) return true;
  if (dir < 0 && cfg.elMinPin >= 0 && digitalRead(cfg.elMinPin) == endstopLvl()) return true;
  return false;
}
// End-stop-guarded stepping — used everywhere the motors actually move. Position mode
// (run) holds at an active stop; speed mode (runSpeed) just declines to step into it.
static inline void azRun()      { long d = azM.distanceToGo(); int dir = (d > 0) - (d < 0); if (dir && azStopHit(dir)) { azM.moveTo(azM.currentPosition()); return; } azM.run(); }
static inline void elRun()      { long d = elM.distanceToGo(); int dir = (d > 0) - (d < 0); if (dir && elStopHit(dir)) { elM.moveTo(elM.currentPosition()); return; } elM.run(); }
static inline void azRunSpeed() { float s = azM.speed(); int dir = (s > 0) - (s < 0); if (dir && azStopHit(dir)) return; azM.runSpeed(); }
static inline void elRunSpeed() { float s = elM.speed(); int dir = (s > 0) - (s < 0); if (dir && elStopHit(dir)) return; elM.runSpeed(); }

// Pointing state received from Overhead. The full shared contract; the rotor reads the
// pointing fields (az/el/az_rate/el_rate/valid/seq) and ignores the reserved radio fields.
volatile telemetry_t rxPkt   = {0};
volatile uint32_t     rxTimeMs = 0;
volatile bool         haveData = false;

// State machine (§9):
//   SCANNING --lock--> HOMING_AZ --> HOMING_EL --> TRACKING <-> PARK
//   PARK --lost > RESCAN_MS--> SCANNING (re-hunt the channel)
//   CALIBRATION: entered on demand (serial menu / boot hold), returns to SCANNING when done.
enum State { SCANNING, HOMING_AZ, HOMING_EL, TRACKING, PARK, CALIBRATION };
State state = SCANNING;

uint8_t  scanIdx = 0;
uint32_t chanChangeMs = 0;
uint8_t  lockedChannel = 0;

// Web-triggered commands are queued here and run from loop() (serviceWeb), so the
// HTTP handler returns instantly and never blocks on motor motion / calibration.
enum WebCmd { WC_NONE, WC_CALAZ, WC_CALEL, WC_SETNORTH, WC_HOME, WC_RESET, WC_REBOOT };
volatile WebCmd g_webCmd = WC_NONE;

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
// DEFERRED (§10.1): the Overhead SENDER side is NOT part of the rotor task. On integration,
// the dashboard adds a broadcast peer that differences its ephemeris into az_rate/el_rate,
// fills the reserved radio fields from the same TLE math, and emits telemetry_t at ~2 Hz to
// FF:FF:FF:FF:FF:FF (even when idle, valid=0). This node is purely the receiver below.
//
// Core 3.x signature. (Core 2.x: void onRecv(const uint8_t* mac, const uint8_t* d, int len))
void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != sizeof(telemetry_t)) return;                       // §4: reject on wrong length
  if (data[0] != TELEM_PROTO_VER) return;                       // §4: reject on proto mismatch (proto_ver is byte 0)
  memcpy((void*)&rxPkt, data, sizeof(telemetry_t));
  rxTimeMs = millis();
  haveData = true;
}

// ----------------------------------------------------------------------------
//  Setup web UI — always-on AP (§ web config). Milestone 2: read-only status.
//  Milestone 3 adds the editable form + calibration buttons. Uses the core
//  WebServer (no extra lib) and shares the radio channel with ESP-NOW.
// ----------------------------------------------------------------------------
WebServer server(80);
void nvsSave();                 // defined below; handleSave persists cfg through it

const char* stateName(State s) {
  switch (s) {
    case SCANNING:    return "SCANNING";   case HOMING_AZ: return "HOMING_AZ";
    case HOMING_EL:   return "HOMING_EL";  case TRACKING:  return "TRACKING";
    case PARK:        return "PARK";       case CALIBRATION: return "CALIBRATION";
    default:          return "?";
  }
}
// form-row helpers (kept terse; the page is assembled as one String)
static String rowNum(const char* label, const char* name, int val, int lo, int hi) {
  return "<tr><th>" + String(label) + "</th><td><input type=number name=" + name +
         " value=" + String(val) + " min=" + String(lo) + " max=" + String(hi) + "></td></tr>";
}
static String rowSel2(const char* label, const char* name, uint8_t v, const char* o0, const char* o1) {
  return "<tr><th>" + String(label) + "</th><td><select name=" + name + ">"
         "<option value=0" + (v == 0 ? " selected" : "") + ">" + o0 + "</option>"
         "<option value=1" + (v == 1 ? " selected" : "") + ">" + o1 + "</option></select></td></tr>";
}

void handleRoot() {
  telemetry_t p; noInterrupts(); memcpy(&p, (const void*)&rxPkt, sizeof(p)); interrupts();
  String h = F("<!doctype html><html lang=en><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Overhead Rotor — setup</title><style>"
    ":root{--bg:#0b0f17;--card:#141b26;--line:#243043;--fg:#e8eef6;--dim:#9fb0c4;--accent:#4ea1ff}"
    "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);"
    "font:15px/1.5 system-ui,Segoe UI,Roboto,sans-serif}.wrap{max-width:640px;margin:0 auto;padding:22px 16px}"
    "h1{font-size:1.4rem;margin:.2em 0}.tag{color:var(--accent);margin:0 0 1em}"
    "table{width:100%;border-collapse:collapse;background:var(--card);border:1px solid var(--line);"
    "border-radius:10px;overflow:hidden;margin-bottom:.4em}td,th{text-align:left;padding:7px 12px;"
    "border-bottom:1px solid var(--line)}th{color:var(--dim);font-weight:600;width:46%}"
    "h2{font-size:1rem;color:var(--dim);margin:1.3em 0 .4em}code{color:#cfe0f5}"
    "input,select,button{background:#0e1420;color:var(--fg);border:1px solid var(--line);border-radius:6px;"
    "padding:5px 8px;font:inherit}input[type=number]{width:5.5rem}"
    "button{background:var(--accent);color:#0b0f17;font-weight:700;cursor:pointer;padding:9px 18px}"
    "a.btn{display:inline-block;background:#0e1420;border:1px solid var(--line);border-radius:6px;"
    "padding:6px 11px;color:var(--accent);text-decoration:none;margin:2px 1px}</style></head>"
    "<body><div class=wrap><h1>Overhead Rotor</h1><p class=tag>setup &amp; status</p>");
  h += F("<h2>Status</h2><table>");
  h += "<tr><th>State</th><td><code>" + String(stateName(state)) + "</code></td></tr>";
  h += "<tr><th>Channel</th><td>" + (lockedChannel ? String(lockedChannel) : String("hunting…")) + "</td></tr>";
  h += "<tr><th>Telemetry</th><td>" + String(haveData ? "receiving" : "none yet") + "</td></tr>";
  h += "<tr><th>Target az / el</th><td>" + String(p.az, 1) + "° / " + String(p.el, 1) + "°</td></tr>";
  h += "<tr><th>az/el steps/deg</th><td>" + String(g_azSpd, 2) + " / " + String(g_elSpd, 2) +
       "  ·  north " + String(g_northOff, 1) + "°</td></tr>";
  h += F("</table><h2>Calibrate / actions</h2><p>"
    "<a class=btn href='/cmd?do=calaz'>CAL AZ</a><a class=btn href='/cmd?do=calel'>CAL EL</a>"
    "<a class=btn href='/cmd?do=setnorth'>SET NORTH</a><a class=btn href='/cmd?do=home'>HOME</a>"
    "<a class=btn href='/cmd?do=reset'>RESET</a></p>"
    "<p class=tag style='color:#9fb0c4;font-size:.82rem'>CAL AZ/EL and HOME move the rotor. "
    "SET NORTH stores the current heading as north.</p>");
  h += F("<h2>Hardware — edit &amp; save</h2><form method=POST action=/save><table>");
  h += rowSel2("Az driver", "azdrv", cfg.azDriver, "unipolar (28BYJ/ULN2003)", "STEP/DIR (NEMA/A4988)");
  h += rowNum("Az pin 1 / STEP", "azp0", cfg.azPins[0], -1, 39);
  h += rowNum("Az pin 2 / DIR",  "azp1", cfg.azPins[1], -1, 39);
  h += rowNum("Az pin 3 / EN",   "azp2", cfg.azPins[2], -1, 39);
  h += rowNum("Az pin 4",        "azp3", cfg.azPins[3], -1, 39);
  h += rowSel2("El driver", "eldrv", cfg.elDriver, "unipolar (28BYJ/ULN2003)", "STEP/DIR (NEMA/A4988)");
  h += rowNum("El pin 1 / STEP", "elp0", cfg.elPins[0], -1, 39);
  h += rowNum("El pin 2 / DIR",  "elp1", cfg.elPins[1], -1, 39);
  h += rowNum("El pin 3 / EN",   "elp2", cfg.elPins[2], -1, 39);
  h += rowNum("El pin 4",        "elp3", cfg.elPins[3], -1, 39);
  h += rowNum("Az home switch",  "azhome", cfg.azHomePin, -1, 39);
  h += rowNum("El home switch (-1=gravity)", "elhome", cfg.elHomePin, -1, 39);
  h += rowNum("Az end-stop CW",  "azcw",  cfg.azCwPin,  -1, 39);
  h += rowNum("Az end-stop CCW", "azccw", cfg.azCcwPin, -1, 39);
  h += rowNum("El end-stop min", "elmin", cfg.elMinPin, -1, 39);
  h += rowNum("El end-stop max", "elmax", cfg.elMaxPin, -1, 39);
  h += rowSel2("Home switch level",  "homeact", cfg.homeActive,    "active-LOW", "active-HIGH");
  h += rowSel2("End-stop level",     "endact",  cfg.endstopActive, "active-LOW", "active-HIGH");
  h += rowNum("ESP-NOW channel (0=auto-hunt)", "chan", cfg.channel, 0, 13);
  h += rowNum("Az backlash comp (steps, 0=off)", "azbl", cfg.azBacklash, 0, 2000);
  h += F("</table><p><button type=submit>Save &amp; reboot</button></p></form>"
    "<p class=tag style='color:#9fb0c4;font-size:.82rem'>Saving reboots to apply pin/driver changes. "
    "You're on the <b>Rotor-setup</b> Wi-Fi.</p></div></body></html>");
  server.send(200, "text/html", h);
}

// clamp a form pin arg to [-1,39]; keep current if the field is absent
static int8_t argPin(const char* k, int8_t cur) {
  if (!server.hasArg(k)) return cur;
  int v = server.arg(k).toInt();
  if (v < -1) v = -1; if (v > 39) v = 39;
  return (int8_t)v;
}
void handleSave() {
  cfg.azDriver = server.arg("azdrv").toInt() ? DRIVER_STEP_DIR : DRIVER_UNIPOLAR_4WIRE;
  cfg.elDriver = server.arg("eldrv").toInt() ? DRIVER_STEP_DIR : DRIVER_UNIPOLAR_4WIRE;
  cfg.azPins[0]=argPin("azp0",cfg.azPins[0]); cfg.azPins[1]=argPin("azp1",cfg.azPins[1]);
  cfg.azPins[2]=argPin("azp2",cfg.azPins[2]); cfg.azPins[3]=argPin("azp3",cfg.azPins[3]);
  cfg.elPins[0]=argPin("elp0",cfg.elPins[0]); cfg.elPins[1]=argPin("elp1",cfg.elPins[1]);
  cfg.elPins[2]=argPin("elp2",cfg.elPins[2]); cfg.elPins[3]=argPin("elp3",cfg.elPins[3]);
  cfg.azHomePin=argPin("azhome",cfg.azHomePin); cfg.elHomePin=argPin("elhome",cfg.elHomePin);
  cfg.azCwPin=argPin("azcw",cfg.azCwPin);   cfg.azCcwPin=argPin("azccw",cfg.azCcwPin);
  cfg.elMinPin=argPin("elmin",cfg.elMinPin); cfg.elMaxPin=argPin("elmax",cfg.elMaxPin);
  cfg.homeActive    = server.arg("homeact").toInt() ? 1 : 0;
  cfg.endstopActive = server.arg("endact").toInt()  ? 1 : 0;
  int ch = server.arg("chan").toInt();
  cfg.channel = (ch >= 1 && ch <= 13) ? (uint8_t)ch : 0;
  int bl = server.arg("azbl").toInt();
  cfg.azBacklash = (bl < 0) ? 0 : (bl > 2000 ? 2000 : (uint16_t)bl);
  nvsSave();
  server.send(200, "text/html",
    F("<!doctype html><meta charset=utf-8><meta http-equiv=refresh content='4;url=/'>"
      "<body style='background:#0b0f17;color:#e8eef6;font-family:system-ui;padding:2rem'>"
      "Saved. Rebooting to apply… <a style='color:#4ea1ff' href='/'>back</a></body>"));
  g_webCmd = WC_REBOOT;
}
void handleCmd() {
  String d = server.arg("do");
  if      (d == "calaz")    g_webCmd = WC_CALAZ;
  else if (d == "calel")    g_webCmd = WC_CALEL;
  else if (d == "setnorth") g_webCmd = WC_SETNORTH;
  else if (d == "home")     g_webCmd = WC_HOME;
  else if (d == "reset")    g_webCmd = WC_RESET;
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "ok");
}

void radioInit() {
  WiFi.mode(WIFI_AP_STA);                              // AP (setup UI) + STA (ESP-NOW) share the radio
  if (strlen(AP_PASS)) WiFi.softAP(AP_SSID, AP_PASS);
  else                 WiFi.softAP(AP_SSID);           // open network
  esp_wifi_set_channel(cfg.channel ? cfg.channel : SCAN_CH[0], WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) { Serial.println("esp_now_init failed"); return; }
  esp_now_register_recv_cb(onRecv);
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/cmd", handleCmd);
  server.begin();
  Serial.printf("[net] AP '%s' up -> http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());
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

// Azimuth backlash compensation — command cfg.azBacklash extra steps in the current
// direction of travel so the gear train always loads the same flank across a reversal
// (the printed-gear pan/tilt heads need this; el uses the accelerometer trim instead).
// The physical output still reaches `ideal`; only a reversal spends the take-up steps.
long g_azPrevTarget = 0;
long g_azBias       = 0;
void azMoveTo(long ideal) {
  if      (ideal > g_azPrevTarget) g_azBias = cfg.azBacklash;   // moving + -> bias ahead
  else if (ideal < g_azPrevTarget) g_azBias = 0;                // moving - -> release bias
  g_azPrevTarget = ideal;
  azM.moveTo(ideal + g_azBias);
}

// ----------------------------------------------------------------------------
//  Channel hunt
// ----------------------------------------------------------------------------
void setChan(uint8_t ch) {
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  chanChangeMs = millis();
}
void startScan() { scanIdx = 0; setChan(cfg.channel ? cfg.channel : SCAN_CH[0]); state = SCANNING; }

void scan() {
  // a valid packet received since we hopped here == Overhead is on this channel
  if (haveData && rxTimeMs >= chanChangeMs) {
    lockedChannel = cfg.channel ? cfg.channel : SCAN_CH[scanIdx];
    Serial.printf("locked on channel %u\n", lockedChannel);
    state = HOMING_AZ;
    return;
  }
  if (cfg.channel) return;                              // channel pinned in config — never hop
  if (WiFi.softAPgetStationNum() > 0) return;           // someone's on the setup AP — hold the channel steady
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
  // hardware cfg blob, layered over the cfgDefaults() seed (ver-guarded)
  if (prefs.isKey("cfg")) {
    RotorCfg tmp;
    if (prefs.getBytes("cfg", &tmp, sizeof(tmp)) == sizeof(tmp) && tmp.ver == cfg.ver) cfg = tmp;
  }
  g_azSpd    = prefs.getFloat("azSpd",  g_azSpd);
  g_elSpd    = prefs.getFloat("elSpd",  g_elSpd);
  g_northOff = prefs.getFloat("north",  g_northOff);
  g_azSign   = prefs.getInt  ("azSign", g_azSign);
  g_elSign   = prefs.getInt  ("elSign", g_elSign);
  prefs.end();
}
void nvsSave() {
  prefs.begin("rotor", false);
  prefs.putBytes("cfg", &cfg, sizeof(cfg));
  prefs.putFloat("azSpd", g_azSpd);   prefs.putFloat("elSpd", g_elSpd);
  prefs.putFloat("north", g_northOff);
  prefs.putInt  ("azSign", g_azSign); prefs.putInt("elSign", g_elSign);
  prefs.end();
}
void nvsReset() {
  prefs.begin("rotor", false); prefs.clear(); prefs.end();
  cfgDefaults();                                  // hardware back to config.h factory defaults
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
  int dir = (azM.speed() > 0) - (azM.speed() < 0);
  while (digitalRead(cfg.azHomePin) != homeLvl()) {
    if (dir && azStopHit(dir)) break;             // an end-stop blocks the path to home
    azM.runSpeed();
  }
  azM.setCurrentPosition(0);
}
void jogAxis(AccelStepper& m, int sign, float spd, float deg) {
  bool isAz = (&m == &azM);                        // pick the axis' end-stop guard
  m.move(sign * lroundf(deg * spd));
  while (m.distanceToGo() != 0) { isAz ? azRun() : elRun(); }
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
    elM.move(block); while (elM.distanceToGo() != 0) elRun();
    delay(300);
    float dp = readPitchAvg() - p0;
    if (fabsf(dp) > 2.0f) {                        // enough travel to trust the ratio
      sum += (float)block / fabsf(dp); n++;
      signVotes += (dp >= 0) ? 1 : -1;            // +steps -> +pitch means sign matches
      Serial.printf("[cal] EL span %d: %ld steps / %.2f deg = %.3f\n", s, block, fabsf(dp), block / fabsf(dp));
    }
    elM.moveTo(from); while (elM.distanceToGo() != 0) elRun();   // return
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
  int  dir = (azM.speed() > 0) - (azM.speed() < 0);
  while (labs(azM.currentPosition()) < maxSteps) {
    if (dir && azStopHit(dir)) break;             // an end-stop stops the full-turn cal
    azM.runSpeed();
    bool active = (digitalRead(cfg.azHomePin) == homeLvl());
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
  cfgDefaults();                 // config.h factory defaults (§5)...
  nvsLoad();                     // ...then NVS overrides hardware cfg + calibration (§8)
  buildSteppers();               // construct steppers from cfg driver + pins (§6)

  pinMode(cfg.azHomePin, INPUT_PULLUP);           // az home switch (required)
  if (cfg.elHomePin >= 0) pinMode(cfg.elHomePin, INPUT_PULLUP);
  if (cfg.azCwPin   >= 0) pinMode(cfg.azCwPin,   INPUT_PULLUP);   // optional end-stops
  if (cfg.azCcwPin  >= 0) pinMode(cfg.azCcwPin,  INPUT_PULLUP);
  if (cfg.elMinPin  >= 0) pinMode(cfg.elMinPin,  INPUT_PULLUP);
  if (cfg.elMaxPin  >= 0) pinMode(cfg.elMaxPin,  INPUT_PULLUP);

  mpuInit();
  azM.setMaxSpeed(AZ_MAX_SPEED); azM.setAcceleration(AZ_ACCEL);
  elM.setMaxSpeed(EL_MAX_SPEED); elM.setAcceleration(EL_ACCEL);
  driverBegin();                 // STEP/DIR enable pins (§6); no-op for unipolar
  showConfig();
  // Optional (§7): hold the limit switch at boot -> a calibration prompt. The serial menu
  // is available in any state, so this is just a hint; motion still proceeds normally.
  if (digitalRead(cfg.azHomePin) == homeLvl())
    Serial.println("[boot] limit held -> send CAL AZ | CAL EL | SETNORTH to calibrate.");
  radioInit();
  startScan();           // find Overhead's channel before doing anything
}

// --- homing: az to the switch; el to a switch (if fitted) else to level -----
void homeAz() {
  azM.setSpeed(g_azSign * -HOME_SPEED);      // drive toward the switch (sign flips with invert)
  if (digitalRead(cfg.azHomePin) == homeLvl()) {
    azM.setCurrentPosition(0);               // mechanical zero
    g_azPrevTarget = 0; g_azBias = 0;        // reset backlash tracking at home
    state = HOMING_EL;
    return;
  }
  azRunSpeed();
}
void homeEl() {
  if (cfg.elHomePin >= 0) {
    // El HOME switch fitted (config): drive down to the horizon switch -> el zero.
    elM.setSpeed(g_elSign * -HOME_SPEED);
    if (digitalRead(cfg.elHomePin) == homeLvl()) {
      elM.setCurrentPosition(0);
      state = TRACKING;
      return;
    }
    elRunSpeed();
  } else {
    // Gravity homing (default): drive to level (accelerometer pitch ~ 0) -> el zero.
    float pitch = readPitchDeg();
    if (fabsf(pitch) < 0.5f) {               // level == elevation 0
      elM.setCurrentPosition(0);
      state = TRACKING;
      return;
    }
    elM.setSpeed(g_elSign * (pitch > 0 ? -HOME_SPEED : HOME_SPEED));
    elRunSpeed();
  }
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

  // Known limitation (§9): near a zenith pass, az slews faster than a 28BYJ can follow, so the
  // rotor lags through overhead and recovers on the far side. Cosmetic for a pointer — this is
  // deliberately NOT "fixed" in software.
  azMoveTo(azDegToSteps(azCmd));   // az with backlash comp
  elM.moveTo(elDegToSteps(elCmd));
  azRun(); elRun();
}

void park() {
  if (millis() - rxTimeMs > RESCAN_MS) { startScan(); return; }  // lost -> re-hunt
  azMoveTo(0);
  elM.moveTo(elDegToSteps(EL_MIN_DEG));
  azRun(); elRun();
  // when a fresh valid packet shows up, resume tracking
  if (haveData && rxPkt.valid && (millis() - rxTimeMs) < PACKET_TIMEOUT_MS) state = TRACKING;
}

// Execute any queued web command (set by the HTTP handlers) in loop context, so
// blocking calibration / motor motion never stalls the web server.
void serviceWeb() {
  WebCmd c = g_webCmd;
  if (c == WC_NONE) return;
  g_webCmd = WC_NONE;
  switch (c) {
    case WC_CALAZ:    state = CALIBRATION; calAz(); startScan(); break;
    case WC_CALEL:    state = CALIBRATION; calEl(); startScan(); break;
    case WC_SETNORTH: setNorth(); break;
    case WC_HOME:     startScan(); state = HOMING_AZ; break;
    case WC_RESET:    nvsReset(); Serial.println("[web] NVS reset -> reboot"); delay(250); ESP.restart(); break;
    case WC_REBOOT:   delay(250); ESP.restart(); break;
    default: break;
  }
}

void loop() {
  server.handleClient();         // setup AP web UI (non-blocking)
  serviceWeb();                  // run any queued web command (cal / home / reset / reboot)
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
