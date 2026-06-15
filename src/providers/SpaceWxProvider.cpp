#include "SpaceWxProvider.h"
#include "../services/Settings.h"
#include "../services/NetClient.h"
#include "../services/Cache.h"
#include "../core/EventBus.h"
#include <ArduinoJson.h>
#include <time.h>

void SpaceWxProvider::begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus) {
  _s = s; _net = net; _cache = cache; _bus = bus;
  String body; CacheMeta m;
  if (_cache->get("swx_kp", body, m) && parseKp(body)) _status = ProviderStatus::Stale;
  if (_cache->get("swx_sfi", body, m)) { JsonDocument d; if (!deserializeJson(d, body)) _sfi = (int)((float)atof((const char*)(d["Flux"] | "-1"))); }
  refresh(false);
}

void SpaceWxProvider::refresh(bool force) {
  uint32_t ttl = (uint32_t)_s->getInt("refreshSpaceWxMin", 20) * 60UL;
  uint32_t now = (uint32_t)time(nullptr);
  CacheMeta m = _cache->stat("swx_kp");
  bool stale = force || !m.found || now < 1600000000UL || (now - m.fetchedAt) > ttl;
  if (stale) { fetchKp(); fetchSfi(); }
}

void SpaceWxProvider::fetchKp() {
  _net->get("https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json",
    [this](int code, const String& body) {
      if (code == 200 && parseKp(body)) {
        _cache->put("swx_kp", body, code, (uint32_t)time(nullptr));
        _lastFetched = (uint32_t)time(nullptr);
        _status = ProviderStatus::Ready;
      } else if (_kp < 0) {
        _status = ProviderStatus::Error;
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
          _sfi = (int)atof((const char*)(d["Flux"] | "-1"));
          _cache->put("swx_sfi", body, code, (uint32_t)time(nullptr));
        }
      }
      if (_bus) _bus->publish(ProviderId::SpaceWx);
    });
}

// Kp feed is an array of rows; row[0] is a header, the last row is most recent:
// [ "time_tag", "Kp", "a_running", "station_count" ].
bool SpaceWxProvider::parseKp(const String& body) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  JsonArray rows = doc.as<JsonArray>();
  if (rows.isNull() || rows.size() < 2) return false;
  JsonArray last = rows[rows.size() - 1].as<JsonArray>();
  if (last.isNull() || last.size() < 2) return false;
  _kp = atof((const char*)(last[1] | "-1"));
  return _kp >= 0;
}
