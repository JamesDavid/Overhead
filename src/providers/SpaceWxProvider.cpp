#include "SpaceWxProvider.h"
#include "../services/Settings.h"
#include "../services/NetClient.h"
#include "../services/Cache.h"
#include "../core/EventBus.h"
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

// NOAA SWPC feeds drift between shapes (object vs array-of-objects, and the key
// case changes: "Flux"/"flux", "kp_index"/"Kp"). Pull the value out tolerantly.
static float numFrom(JsonVariant v) {
  if (v.isNull()) return -1;
  if (v.is<const char*>()) { const char* s = (const char*)v; if (!s || !(s[0]=='-'||s[0]=='.'||(s[0]>='0'&&s[0]<='9'))) return -1; return (float)atof(s); }
  if (v.is<float>() || v.is<int>()) return v.as<float>();
  return -1;
}
static int fluxFromObj(JsonObject o) {
  for (const char* k : {"flux", "Flux", "f10.7", "f107"}) { float f = numFrom(o[k]); if (f >= 0) return (int)lround(f); }
  return -1;
}
// Accept either a bare object or an array of samples (take the newest with flux).
static int extractSfi(JsonVariant root) {
  if (root.is<JsonObject>()) return fluxFromObj(root.as<JsonObject>());
  if (root.is<JsonArray>()) {
    JsonArray a = root.as<JsonArray>();
    for (int i = (int)a.size() - 1; i >= 0; --i)
      if (a[i].is<JsonObject>()) { int f = fluxFromObj(a[i].as<JsonObject>()); if (f >= 0) return f; }
  }
  return -1;
}

void SpaceWxProvider::begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus) {
  _s = s; _net = net; _cache = cache; _bus = bus;
  String body; CacheMeta m;
  if (_cache->get("swx_kp", body, m) && parseKp(body)) {
    _status = ProviderStatus::Stale;
    _lastFetched = m.fetchedAt;                   // persist freshness across reboots
  }
  if (_cache->get("swx_sfi", body, m)) { JsonDocument d; if (!deserializeJson(d, body)) _sfi = extractSfi(d); }
  refresh(false);
}

void SpaceWxProvider::refresh(bool force) {
  uint32_t ttl = (uint32_t)_s->getInt("refreshSpaceWxMin", 20) * 60UL;
  uint32_t now = (uint32_t)time(nullptr);
  CacheMeta m = _cache->stat("swx_kp");
  bool stale = force || !m.found || (now >= 1600000000UL && (now - m.fetchedAt) > ttl);  // trust cache pre-NTP
  if (stale) { fetchKp(); fetchSfi(); fetchXray(); fetchWind(); fetchMag(); }
  else if (_kp >= 0) _status = ProviderStatus::Ready;   // fresh persisted cache is ready
}

// Latest GOES X-ray flare class (e.g. "M1.2"); tiny single-element array.
void SpaceWxProvider::fetchXray() {
  _net->get("https://services.swpc.noaa.gov/json/goes/primary/xray-flares-latest.json",
    [this](int code, const String& body) {
      if (code == 200) {
        JsonDocument d;
        if (!deserializeJson(d, body)) {
          JsonArray a = d.as<JsonArray>();
          if (!a.isNull() && a.size()) _flare = (const char*)(a[0]["current_class"] | "");
        }
      }
      if (_bus) _bus->publish(ProviderId::SpaceWx);
    });
}

// Solar-wind speed (km/s) + IMF Bz (nT) from the compact SWPC summary endpoints.
void SpaceWxProvider::fetchWind() {
  _net->get("https://services.swpc.noaa.gov/products/summary/solar-wind-speed.json",
    [this](int code, const String& body) {
      if (code == 200) {
        JsonDocument d;
        if (!deserializeJson(d, body)) {
          JsonArray a = d.as<JsonArray>();
          if (!a.isNull() && a.size()) _windKms = (int)lround((float)(a[0]["proton_speed"] | -1.0));
        }
      }
      if (_bus) _bus->publish(ProviderId::SpaceWx);
    });
}

void SpaceWxProvider::fetchMag() {
  _net->get("https://services.swpc.noaa.gov/products/summary/solar-wind-mag-field.json",
    [this](int code, const String& body) {
      if (code == 200) {
        JsonDocument d;
        if (!deserializeJson(d, body)) {
          JsonArray a = d.as<JsonArray>();
          if (!a.isNull() && a.size()) _bz = (int)lround((float)(a[0]["bz_gsm"] | -999.0));
        }
      }
      if (_bus) _bus->publish(ProviderId::SpaceWx);
    });
}

void SpaceWxProvider::fetchKp() {
  _net->get("https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json",
    [this](int code, const String& body) {
      if (code == 200 && parseKp(body)) {
        _cache->put("swx_kp", body, code, (uint32_t)time(nullptr));
        _lastFetched = (uint32_t)time(nullptr);
        _status = ProviderStatus::Ready;
        Serial.printf("[spacewx] Kp=%.1f\n", _kp);
      } else {
        Serial.printf("[spacewx] Kp fetch failed code=%d len=%u\n", code, (unsigned)body.length());
        if (_kp < 0) _status = ProviderStatus::Error;
      }
      if (_bus) _bus->publish(ProviderId::SpaceWx);
    });
}

void SpaceWxProvider::fetchSfi() {
  _net->get("https://services.swpc.noaa.gov/products/summary/10cm-flux.json",
    [this](int code, const String& body) {
      if (code == 200) {
        JsonDocument d;
        if (!deserializeJson(d, body)) {
          _sfi = extractSfi(d);
          if (_sfi >= 0) { _cache->put("swx_sfi", body, code, (uint32_t)time(nullptr)); Serial.printf("[spacewx] SFI=%d\n", _sfi); }
          else            Serial.printf("[spacewx] SFI not found, body: %.90s\n", body.c_str());
        }
      } else {
        Serial.printf("[spacewx] SFI fetch code=%d\n", code);
      }
      if (_bus) _bus->publish(ProviderId::SpaceWx);
    });
}

// Kp feed is an array of rows; row[0] is a header, the last row is most recent:
// [ "time_tag", "Kp", "a_running", "station_count" ].
bool SpaceWxProvider::parseKp(const String& body) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) { Serial.printf("[spacewx] kp parse err: %s\n", err.c_str()); return false; }
  JsonArray rows = doc.as<JsonArray>();
  if (rows.isNull() || rows.size() < 2) { Serial.println("[spacewx] kp: not an array"); return false; }
  // Walk forward, collecting every numeric Kp (the feed is array-of-arrays
  // [time, Kp, ...] with a header row, or array-of-objects {"kp_index":..}/{"Kp":..};
  // trailing slots are often blank). The last numeric is the latest Kp; keep the
  // most recent kKpHist values for the sparkline.
  _kpHistN = 0;
  for (size_t i = 0; i < rows.size(); ++i) {
    JsonVariant row = rows[i];
    float v = -1;
    if (row.is<JsonArray>()) {
      JsonArray r = row.as<JsonArray>();
      if (r.size() >= 2) v = numFrom(r[1]);                 // header "Kp" -> numFrom rejects
    } else if (row.is<JsonObject>()) {
      JsonObject o = row.as<JsonObject>();
      for (const char* k : {"kp_index", "kp", "estimated_kp", "Kp"}) { v = numFrom(o[k]); if (v >= 0) break; }
    }
    if (v < 0) continue;
    _kp = v;
    if (_kpHistN < kKpHist) _kpHist[_kpHistN++] = v;
    else { for (int j = 1; j < kKpHist; ++j) _kpHist[j - 1] = _kpHist[j]; _kpHist[kKpHist - 1] = v; }
  }
  if (_kpHistN == 0) { Serial.printf("[spacewx] kp: no numeric row; body: %.110s\n", body.c_str()); return false; }
  Serial.printf("[spacewx] Kp=%.1f (%d hist)\n", _kp, _kpHistN);
  return true;
}
