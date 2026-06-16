#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// services/Settings — runtime settings store on LittleFS (spec §1, §4). Anything
// the user can change without recompiling lives here (location, units, theme,
// refresh intervals, OTA creds, focus prefs). `config.h` stays build-time only.
//
// Carries a settingsVersion and migrates on boot so an OTA that changes the
// schema doesn't wipe saved prefs / the watchlist (spec §13).
//
// Milestone 1 uses flat top-level keys for simplicity; nested presets/watchlists
// arrive with the Location overlay and filtering (spec §6.6, §6 Location).
class Settings {
public:
  bool begin();          // load /settings.json, or seed + save defaults
  bool save();

  String getString(const char* key, const char* def = "");
  long   getInt(const char* key, long def = 0);
  bool   getBool(const char* key, bool def = false);
  double getFloat(const char* key, double def = 0);

  void set(const char* key, const char* v);
  void set(const char* key, long v);
  void set(const char* key, bool v);
  void set(const char* key, double v);

  JsonDocument& doc() { return _doc; }
  int  version() const { return _doc["settingsVersion"] | 0; }

private:
  void seedDefaults();
  void backfillDefaults();        // add missing default keys without clobbering
  void fillDefaults(JsonDocument& d);
  void migrate();

  JsonDocument _doc;
  static constexpr const char* kPath = "/settings.json";
  static constexpr int kVersion = 4;
};
