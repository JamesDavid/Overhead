#include "WeatherProvider.h"
#include "../services/Settings.h"
#include "../services/NetClient.h"
#include "../services/Cache.h"
#include "../services/LocationService.h"
#include "../core/EventBus.h"
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

// "YYYY-MM-DDTHH:MM" (UTC, timezone=GMT) -> epoch.
static time_t isoToEpoch(const String& iso) {
  int y, mo, d, h, mi;
  if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) < 5) return 0;
  if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;   // mo=0 would index cum[-1] (negative C++ modulo)
  static const int cum[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  long days = (long)(y - 1970) * 365 + (y - 1969) / 4 - (y - 1901) / 100 + (y - 1601) / 400
            + cum[mo - 1] + (d - 1);
  if (mo > 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) days += 1;
  return (time_t)days * 86400 + h * 3600 + mi * 60;
}

void WeatherProvider::begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus, LocationService* loc) {
  _s = s; _net = net; _cache = cache; _bus = bus; _loc = loc;
  String body; CacheMeta m;
  if (_cache->get("weather", body, m) && parse(body)) {       // last-good cache: usable, offline-friendly
    if (m.fetchedAt > 1600000000UL) _lastFetched = m.fetchedAt;  // restore freshness across reboots
    _status = ProviderStatus::Stale;                          // have data -> not "Loading"
  }
  refresh(false);
}

void WeatherProvider::refresh(bool force) {
  if (_inflight || !_loc->active().valid) return;
  uint32_t ttl = (uint32_t)_s->getInt("refreshWeatherMin", 45) * 60UL;
  uint32_t now = (uint32_t)time(nullptr);
  CacheMeta m = _cache->stat("weather");
  bool stale = force || !m.found || now < 1600000000UL || (now - m.fetchedAt) > ttl;
  if (!stale) { if (!_cloud.empty()) _status = ProviderStatus::Ready; return; }  // fresh cache = ready

  char url[260];
  snprintf(url, sizeof(url),
    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
    "&hourly=cloud_cover,precipitation_probability,temperature_2m,dew_point_2m,surface_pressure,wind_speed_10m,wind_direction_10m"
    "&wind_speed_unit=kn&forecast_days=2&timezone=GMT",
    _loc->active().lat, _loc->active().lon);
  _inflight = true;
  bool sent = _net->get(url, [this](int code, const String& body) {
    _inflight = false;
    if (code == 200 && parse(body)) {
      uint32_t nowt = (uint32_t)time(nullptr);
      _cache->put("weather", body, code, nowt);
      if (nowt > 1600000000UL) _lastFetched = nowt;   // don't stamp a pre-NTP (epoch-0-ish) time
      _status = ProviderStatus::Ready;
    } else if (_cloud.empty()) {
      _status = ProviderStatus::Error;
    }
    if (_bus) _bus->publish(ProviderId::Weather);
  });
  if (!sent) _inflight = false;   // queue full -> retry next cycle instead of wedging
}

bool WeatherProvider::parse(const String& body) {
  JsonDocument filter;
  filter["hourly"]["time"] = true;
  filter["hourly"]["cloud_cover"] = true;
  filter["hourly"]["precipitation_probability"] = true;
  filter["hourly"]["temperature_2m"] = true;
  filter["hourly"]["dew_point_2m"] = true;
  filter["hourly"]["surface_pressure"] = true;
  filter["hourly"]["wind_speed_10m"] = true;
  filter["hourly"]["wind_direction_10m"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;

  JsonArray t = doc["hourly"]["time"].as<JsonArray>();
  JsonArray c = doc["hourly"]["cloud_cover"].as<JsonArray>();
  JsonArray p = doc["hourly"]["precipitation_probability"].as<JsonArray>();
  if (t.isNull() || c.isNull() || t.size() < 2) return false;

  _base = isoToEpoch((const char*)(t[0] | ""));
  _cloud.clear(); _precip.clear(); _temp.clear(); _dewp.clear(); _pres.clear(); _wind.clear(); _windDir.clear();
  for (JsonVariant v : c) _cloud.push_back((int8_t)(int)(v | -1));
  if (!p.isNull()) for (JsonVariant v : p) _precip.push_back((int8_t)(int)(v | -1));
  JsonArray tp = doc["hourly"]["temperature_2m"].as<JsonArray>();
  JsonArray dp = doc["hourly"]["dew_point_2m"].as<JsonArray>();
  JsonArray pr = doc["hourly"]["surface_pressure"].as<JsonArray>();
  if (!tp.isNull()) for (JsonVariant v : tp) _temp.push_back((int8_t)lround(v | 0.0f));
  if (!dp.isNull()) for (JsonVariant v : dp) _dewp.push_back((int8_t)lround(v | 0.0f));
  if (!pr.isNull()) for (JsonVariant v : pr) _pres.push_back((int16_t)lround(v | 0.0f));
  JsonArray ws = doc["hourly"]["wind_speed_10m"].as<JsonArray>();
  JsonArray wd = doc["hourly"]["wind_direction_10m"].as<JsonArray>();
  if (!ws.isNull()) for (JsonVariant v : ws) _wind.push_back((int8_t)lround(v | 0.0f));
  if (!wd.isNull()) for (JsonVariant v : wd) _windDir.push_back((int16_t)lround(v | 0.0f));
  return !_cloud.empty() && _base > 0;
}

int WeatherProvider::cloudCoverAt(time_t t) const {
  if (_cloud.empty() || _base == 0) return -1;
  long idx = (long)(t - _base) / 3600;
  if (idx < 0 || idx >= (long)_cloud.size()) return -1;
  return _cloud[idx];
}

int WeatherProvider::precipProbAt(time_t t) const {
  if (_precip.empty() || _base == 0) return -1;
  long idx = (long)(t - _base) / 3600;
  if (idx < 0 || idx >= (long)_precip.size()) return -1;
  return _precip[idx];
}
