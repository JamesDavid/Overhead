#include "PressureMapProvider.h"
#include "../services/Settings.h"
#include "../services/NetClient.h"
#include "../services/Cache.h"
#include "../services/LocationService.h"
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

// Built-in station spreads. icao + coords (the feed only supplies altim + cloud).
struct Airport { const char* icao; float lat, lon; };
static const Airport kUS[] = {
  {"KSEA",47.45f,-122.31f},{"KSFO",37.62f,-122.38f},{"KLAX",33.94f,-118.41f},{"KLAS",36.08f,-115.15f},
  {"KPHX",33.43f,-112.01f},{"KSLC",40.79f,-111.98f},{"KDEN",39.86f,-104.67f},{"KBIL",45.81f,-108.54f},
  {"KDFW",32.90f,-97.04f}, {"KIAH",29.98f,-95.34f}, {"KMCI",39.30f,-94.71f}, {"KMSP",44.88f,-93.22f},
  {"KORD",41.98f,-87.90f}, {"KATL",33.64f,-84.43f}, {"KMIA",25.79f,-80.29f}, {"KDTW",42.21f,-83.35f},
  {"KJFK",40.64f,-73.78f}, {"KBOS",42.36f,-71.01f},
};
static const Airport kWORLD[] = {
  {"KLAX",34.0f,-118.4f},{"KJFK",40.6f,-73.8f},{"CYYZ",43.7f,-79.6f},{"SBGR",-23.4f,-46.5f},
  {"SAEZ",-34.8f,-58.5f},{"EGLL",51.5f,-0.45f},{"LFPG",49.0f,2.5f},  {"EDDF",50.0f,8.6f},
  {"UUEE",56.0f,37.4f},  {"OMDB",25.3f,55.4f}, {"FAOR",-26.1f,28.2f},{"VIDP",28.6f,77.1f},
  {"ZBAA",40.1f,116.6f}, {"RJTT",35.6f,139.8f},{"YSSY",-33.9f,151.2f},{"NZAA",-37.0f,174.8f},
};

void PressureMapProvider::begin(Settings* s, NetClient* net, Cache* cache, LocationService* loc) {
  _s = s; _net = net; _cache = cache; _loc = loc;
  String body; CacheMeta m;
  if (_cache->get("presmap", body, m) && parse(body)) _status = ProviderStatus::Stale;
  refresh(false);
}

void PressureMapProvider::refresh(bool force) {
  if (_inflight || !_loc->active().valid) return;
  uint32_t ttl = 45UL * 60UL;                       // surface pattern shifts slowly
  uint32_t now = (uint32_t)time(nullptr);
  CacheMeta m = _cache->stat("presmap");
  bool stale = force || !m.found || now < 1600000000UL || (now - m.fetchedAt) > ttl;
  if (!stale) return;

  // Choose the station set by the observer's location (continental-US bbox vs world).
  double la = _loc->active().lat, lo = _loc->active().lon;
  _world = !(la > 24 && la < 50 && lo > -126 && lo < -66);
  const Airport* set = _world ? kWORLD : kUS;
  int n = _world ? (int)(sizeof(kWORLD) / sizeof(kWORLD[0])) : (int)(sizeof(kUS) / sizeof(kUS[0]));

  String url = "https://aviationweather.gov/api/data/metar?format=json&ids=";
  for (int i = 0; i < n; ++i) { if (i) url += ","; url += set[i].icao; }

  _inflight = true;
  _net->get(url, [this](int code, const String& body) {
    _inflight = false;
    if (code == 200 && parse(body)) {
      _cache->put("presmap", body, code, (uint32_t)time(nullptr));
      _lastFetched = (uint32_t)time(nullptr);
      _status = ProviderStatus::Ready;
    } else if (_pts.empty()) {
      _status = ProviderStatus::Error;
    }
  });
}

static int coverPct(const String& c) {
  if (c == "OVC") return 100; if (c == "BKN") return 75;
  if (c == "SCT") return 40;  if (c == "FEW") return 20;
  return 0;
}

bool PressureMapProvider::parse(const String& body) {
  JsonDocument filter;
  JsonObject e = filter.add<JsonObject>();
  e["icaoId"] = e["altim"] = true;
  JsonObject c = e["clouds"].add<JsonObject>();
  c["cover"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) return false;

  // Look up coords from whichever set is active.
  const Airport* set = _world ? kWORLD : kUS;
  int n = _world ? (int)(sizeof(kWORLD) / sizeof(kWORLD[0])) : (int)(sizeof(kUS) / sizeof(kUS[0]));

  std::vector<PressurePt> out;
  for (JsonObject o : arr) {
    String id = (const char*)(o["icaoId"] | "");
    if (!o["altim"].is<float>()) continue;
    PressurePt p; p.icao = id;
    p.hpa = (int)lround((float)o["altim"]);
    int cl = 0;
    for (JsonObject c2 : o["clouds"].as<JsonArray>()) { int v = coverPct((const char*)(c2["cover"] | "")); if (v > cl) cl = v; }
    p.cloud = cl;
    for (int i = 0; i < n; ++i) if (id == set[i].icao) { p.lat = set[i].lat; p.lon = set[i].lon; break; }
    if (p.lat != 0 || p.lon != 0) out.push_back(p);
  }
  if (out.empty()) return false;
  _pts = std::move(out);
  return true;
}
