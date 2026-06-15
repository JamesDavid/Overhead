#include "Settings.h"
#include <LittleFS.h>

bool Settings::begin() {
  File f = LittleFS.open(kPath, "r");
  if (f) {
    DeserializationError err = deserializeJson(_doc, f);
    f.close();
    if (!err) { migrate(); return true; }
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
  // Seed the watchlist so the Director is useful on first boot (spec §13).
  JsonArray wl = _doc["watchlist"].to<JsonArray>();
  wl.add("ISS");
  wl.add("SO-50");
  wl.add("AO-91");
  _doc["satWatchlistOnly"] = true;    // Satellites selector walks the watchlist
}

void Settings::migrate() {
  int v = version();
  if (v == kVersion) return;
  Serial.printf("[settings] migrating v%d -> v%d\n", v, kVersion);
  // v0/unknown -> v1: fill any missing keys without clobbering existing ones.
  if (!_doc["locMode"].is<const char*>()) _doc["locMode"] = "auto";
  if (!_doc["otaUser"].is<const char*>()) _doc["otaUser"] = "admin";
  if (!_doc["otaPass"].is<const char*>()) _doc["otaPass"] = "overhead";
  _doc["settingsVersion"] = kVersion;
  save();
}

String Settings::getString(const char* key, const char* def) { return String((const char*)(_doc[key] | def)); }
long   Settings::getInt(const char* key, long def)           { return _doc[key] | def; }
bool   Settings::getBool(const char* key, bool def)          { return _doc[key] | def; }
double Settings::getFloat(const char* key, double def)       { return _doc[key] | def; }

void Settings::set(const char* key, const char* v) { _doc[key] = v; }
void Settings::set(const char* key, long v)        { _doc[key] = v; }
void Settings::set(const char* key, bool v)        { _doc[key] = v; }
void Settings::set(const char* key, double v)      { _doc[key] = v; }
