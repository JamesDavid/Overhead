#include "Settings.h"
#include <LittleFS.h>

bool Settings::begin() {
  File f = LittleFS.open(kPath, "r");
  if (f) {
    DeserializationError err = deserializeJson(_doc, f);
    f.close();
    if (!err) {
      backfillDefaults();   // add any keys this firmware introduced (no clobber)
      migrate();
      return save();        // persist backfilled keys so the web form never sees blanks
    }
    Serial.printf("[settings] parse error (%s) — reseeding\n", err.c_str());
  }
  seedDefaults();
  return save();
}

bool Settings::save() {
  File f = LittleFS.open(kPath, "w");
  if (!f) { Serial.println("[settings] WARN: cannot open for write"); return false; }
  serializeJson(_doc, f);
  f.close();
  return true;
}

void Settings::seedDefaults() {
  _doc.clear();
  fillDefaults(_doc);
}

// Copy any default key that is missing from _doc (never overwrites existing values).
void Settings::backfillDefaults() {
  JsonDocument def;
  fillDefaults(def);
  for (JsonPair kv : def.as<JsonObject>())
    if (_doc[kv.key()].isNull()) _doc[kv.key()] = kv.value();
}

void Settings::fillDefaults(JsonDocument& _doc) {
  _doc["settingsVersion"] = kVersion;
  // Location: Auto (IP geolocation) by default (spec §6 Location, §13).
  _doc["locMode"]  = "auto";          // auto | preset | gps
  _doc["locName"]  = "Auto (IP)";
  _doc["locLat"]   = 0.0;
  _doc["locLon"]   = 0.0;
  _doc["tzOffset"] = 0;               // seconds; refined from IP/Open-Meteo
  _doc["units"]    = "metric";
  _doc["theme"]    = "auto";          // auto | day | night
  // OTA / settings page basic-auth (spec §13). CHANGE THESE on a shared LAN.
  _doc["otaUser"]  = "admin";
  _doc["otaPass"]  = "overhead";
  // Default refresh intervals (minutes) — providers read these later.
  _doc["refreshLaunchMin"]  = 45;
  _doc["refreshTleHour"]    = 12;
  _doc["refreshSpaceWxMin"] = 20;
  _doc["refreshWeatherMin"] = 45;
  _doc["refreshAvWxMin"]    = 12;     // aviation METAR/TAF refresh
  // Seed the watchlist so the Director is useful on first boot (spec §13).
  JsonArray wl = _doc["watchlist"].to<JsonArray>();
  wl.add("ZARYA");           // "ISS (ZARYA)" — exact-ish; bare "ISS" CONTAINS-matches co-orbiting "ISS OBJECT.." junk
  wl.add("SO-50");           // "SAUDISAT 1C (SO-50)"
  wl.add("FOX-1B");          // AO-91 = "RADFXSAT (FOX-1B)"
  wl.add("SATGUS");          // CrunchLabs "SatGus"
  _doc["satWatchlistOnly"] = true;    // Satellites selector walks the watchlist
  _doc["satMinEl"]         = 10;      // min pass elevation (deg) — kills grazers
  // Aircraft (spec §6): cloud by default; local readsb/tar1090 feeder optional.
  _doc["adsbMode"]      = "cloud";    // cloud | local
  _doc["adsbHost"]      = "";         // local feeder host/ip (e.g. 192.168.1.50)
  _doc["adsbRadiusNm"]  = 50;
  _doc["adsbPollSec"]   = 5;
  _doc["adsbMaxAltFt"]  = 0;          // 0 = no altitude cap
  // Appearance / ThemeController (spec §7.9)
  _doc["themeMode"]     = "auto";     // auto | day | night
  _doc["nightPalette"]  = "dark";     // dark | red (dark-adapt)
  _doc["themeNightAlt"] = -6;         // Sun alt (deg): day -> night/twilight (auto)
  _doc["themeRedAlt"]   = -12;        // Sun alt (deg): night -> red dark-adapt (auto, needs nightPalette=red)
  _doc["nightBacklight"]= 90;         // 0..255 at night
  _doc["backlight"]     = 0;          // manual brightness override (0 = auto day/night)
  // Director / Intelligent Focus (spec §7.10)
  _doc["focusEnabled"]  = true;
  _doc["ambientDay"]    = "Agenda";         // page title for day ambient
  _doc["ambientNight"]  = "Solar System,Star Map";   // night ambient rotation (CSV of page titles)
  _doc["orreryBodies"]  = "Roadster,Psyche,Ceres,Vesta";  // minor bodies shown on the orrery (CSV)
  _doc["nightAmbientAlt"] = -12;      // Sun alt to switch to the night ambient tab
  _doc["passLeadMin"]   = 5;          // minutes before AOS to seize focus
  _doc["launchLeadMin"] = 10;         // minutes before T-0 to seize focus
  _doc["alertSat"]      = true;       // which pages may raise the cross-tab alert banner
  _doc["alertLaunch"]   = true;
  _doc["alertAircraft"] = false;      // plane-overhead alert off by default (can be chatty)
  _doc["alertWx"]       = true;
  // Audio: Morse-code alert beeper (beeps the alert's first word -- ISS, FALCON...)
  _doc["audioEnabled"]       = false; // master enable (opt-in; needs a buzzer/speaker)
  _doc["audioKochWpm"]       = 18;    // character/element speed (WPM)
  _doc["audioFarnsworthWpm"] = 12;    // effective speed (stretches inter-char gaps; clamped <= Koch)
  _doc["audioBeepAtNight"]   = false; // also beep during the night theme tier
  _doc["audioToneHz"]        = 650;   // PWM tone pitch (CYD speaker etc.); on/off buzzers ignore it
  _doc["inactivitySec"] = 90;         // MANUAL -> AUTO after this idle time
  _doc["dimAfterSec"]   = 120;        // backlight dims after this idle time (spec §13)
  _doc["dimLevel"]      = 20;         // dimmed backlight (0..255)
}

void Settings::migrate() {
  int v = version();
  if (v == kVersion) return;
  Serial.printf("[settings] migrating v%d -> v%d\n", v, kVersion);
  // v0/unknown -> fill any missing keys without clobbering existing ones.
  if (!_doc["locMode"].is<const char*>()) _doc["locMode"] = "auto";
  if (!_doc["otaUser"].is<const char*>()) _doc["otaUser"] = "admin";
  if (!_doc["otaPass"].is<const char*>()) _doc["otaPass"] = "overhead";
  // -> v3: the day-ambient default is the Agenda home tab (force; the old default
  // was Launches and the v2 conditional migration didn't take on some units).
  if (v < 3) _doc["ambientDay"] = "Agenda";
  // -> v4: night ambient is a rotation (Solar System + Star Map). Only upgrade the
  // old single default; leave a user's custom choice alone.
  if (v < 4 && String((const char*)(_doc["ambientNight"] | "")) == "Solar System")
    _doc["ambientNight"] = "Solar System,Star Map";
  _doc["settingsVersion"] = kVersion;
  // (begin() saves after backfill + migrate, so no save() here)
}

String Settings::getString(const char* key, const char* def) { return String((const char*)(_doc[key] | def)); }
long   Settings::getInt(const char* key, long def)           { return _doc[key] | def; }
bool   Settings::getBool(const char* key, bool def)          { return _doc[key] | def; }
double Settings::getFloat(const char* key, double def)       { return _doc[key] | def; }

void Settings::set(const char* key, const char* v) { _doc[key] = v; }
void Settings::set(const char* key, long v)        { _doc[key] = v; }
void Settings::set(const char* key, bool v)        { _doc[key] = v; }
void Settings::set(const char* key, double v)      { _doc[key] = v; }
