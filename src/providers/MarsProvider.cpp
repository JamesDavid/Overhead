#include "MarsProvider.h"
#include "../services/Settings.h"
#include "../services/NetClient.h"
#include "../services/Cache.h"
#include "../core/EventBus.h"
#include <ArduinoJson.h>
#include <time.h>

static const char* kCacheKey = "mars_rovers";

void MarsProvider::begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus) {
  _s = s; _net = net; _cache = cache; _bus = bus;
  String body; CacheMeta m;
  if (_cache->get(kCacheKey, body, m) && parse(body)) _status = ProviderStatus::Stale;
  refresh(false);
}

void MarsProvider::refresh(bool force) {
  if (_inflight) return;
  uint32_t ttl = 6UL * 3600UL;                      // rovers update ~daily; 6h is plenty
  uint32_t now = (uint32_t)time(nullptr);
  CacheMeta m = _cache->stat(kCacheKey);
  bool stale = force || !m.found || now < 1600000000UL || (now - m.fetchedAt) > ttl;
  if (!stale) return;

  // /rovers returns all rover summaries in one small body (no per-photo lists).
  const char* url = "https://api.nasa.gov/mars-photos/api/v1/rovers?api_key=DEMO_KEY";
  _inflight = true;
  bool sent = _net->get(url, [this](int code, const String& body) {
    _inflight = false;
    if (code == 200 && parse(body)) {
      _cache->put(kCacheKey, body, code, (uint32_t)time(nullptr));
      _lastFetched = (uint32_t)time(nullptr);
      _status = ProviderStatus::Ready;
    } else if (_persev.maxSol < 0 && _curio.maxSol < 0) {
      _status = ProviderStatus::Error;
    }
    if (_bus) _bus->publish(ProviderId::Mars);
  });
  if (!sent) _inflight = false;   // queue full -> retry next cycle instead of wedging
}

bool MarsProvider::parse(const String& body) {
  JsonDocument filter;
  JsonObject r = filter["rovers"].add<JsonObject>();
  r["name"] = r["status"] = r["max_sol"] = r["max_date"] = r["total_photos"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  JsonArray rovers = doc["rovers"].as<JsonArray>();
  if (rovers.isNull()) return false;

  bool got = false;
  for (JsonObject o : rovers) {
    String name = (const char*)(o["name"] | "");
    RoverInfo* dst = name.equalsIgnoreCase("perseverance") ? &_persev
                   : name.equalsIgnoreCase("curiosity")    ? &_curio : nullptr;
    if (!dst) continue;
    dst->name        = name;
    dst->status      = (const char*)(o["status"] | "");
    dst->maxSol      = o["max_sol"] | -1;
    dst->maxDate     = (const char*)(o["max_date"] | "");
    dst->totalPhotos = o["total_photos"] | -1;
    got = true;
  }
  return got;
}
