#include "PressureMapProvider.h"
#include "../services/Settings.h"
#include "../services/NetClient.h"
#include "../services/Cache.h"
#include "../services/LocationService.h"
#include "../services/MetarStore.h"
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

void PressureMapProvider::computeBbox() {
  if (_scope == 2) { _w0 = -180; _w1 = 180; _a0 = -60; _a1 = 78; return; }   // world
  if (_scope == 1) { _w0 = -126; _w1 = -66; _a0 = 24;  _a1 = 50; return; }   // continental US
  double la = _custom ? _cLat : (_loc && _loc->active().valid) ? _loc->active().lat : 39.0;   // regional box
  double lo = _custom ? _cLon : (_loc && _loc->active().valid) ? _loc->active().lon : -98.0;
  double cl = cos(la * M_PI / 180.0); if (cl < 0.3) cl = 0.3;
  const double dlat = 2.9;                          // ~200 mi radius
  _a0 = la - dlat; _a1 = la + dlat; _w0 = lo - dlat / cl; _w1 = lo + dlat / cl;
}

void PressureMapProvider::fetchAround(double lat, double lon) {
  if (_inflight) return;
  _scope = 0; _custom = true; _cLat = lat; _cLon = lon;   // regional box centred on the drilled-in point
  computeBbox();
  _pts.clear();
  char bb[80]; snprintf(bb, sizeof(bb), "bbox=%.2f,%.2f,%.2f,%.2f", _a0, _w0, _a1, _w1);
  String url = String("https://aviationweather.gov/api/data/metar?format=json&") + bb;
  _inflight = true; _status = ProviderStatus::Loading;
  _net->get(url, [this](int code, const String& body) {     // ad-hoc drill-in: not cached
    _inflight = false;
    if (code == 200 && parse(body)) { _lastFetched = (uint32_t)time(nullptr); _status = ProviderStatus::Ready; }
    else if (_pts.empty()) _status = ProviderStatus::Error;
  });
}

void PressureMapProvider::setScope(int s) {
  if (s < 0) s = 0; if (s > 2) s = 2;
  _custom = false;                                   // leaving any drilled-in region
  if (s == _scope) { computeBbox(); return; }
  _scope = s; computeBbox();
  String body; CacheMeta m;                         // serve cache for the new scope while refetching
  _pts.clear();
  if (_cache->get((String("presmap") + _scope).c_str(), body, m) && parse(body)) _status = ProviderStatus::Stale;
  refresh(true);
}

void PressureMapProvider::begin(Settings* s, NetClient* net, Cache* cache, LocationService* loc) {
  _s = s; _net = net; _cache = cache; _loc = loc;
  computeBbox();
  String body; CacheMeta m;
  if (_cache->get((String("presmap") + _scope).c_str(), body, m) && parse(body)) _status = ProviderStatus::Stale;
  refresh(false);
}

void PressureMapProvider::refresh(bool force) {
  if (_inflight || !_loc->active().valid) return;
  uint32_t ttl = 45UL * 60UL;                       // surface pattern shifts slowly
  uint32_t now = (uint32_t)time(nullptr);
  String key = String("presmap") + _scope;
  CacheMeta m = _cache->stat(key.c_str());
  bool stale = force || !m.found || (now >= 1600000000UL && (now - m.fetchedAt) > ttl);
  if (!stale) { if (!_pts.empty()) _status = ProviderStatus::Ready; return; }

  computeBbox();
  String url = "https://aviationweather.gov/api/data/metar?format=json&";
  if (_scope == 0) {                                // regional: query by bounding box
    char bb[80]; snprintf(bb, sizeof(bb), "bbox=%.2f,%.2f,%.2f,%.2f", _a0, _w0, _a1, _w1);
    url += bb;
  } else {                                          // US / world: the fixed station spread
    const Airport* set = _scope == 2 ? kWORLD : kUS;
    int n = _scope == 2 ? (int)(sizeof(kWORLD) / sizeof(kWORLD[0])) : (int)(sizeof(kUS) / sizeof(kUS[0]));
    url += "ids=";
    for (int i = 0; i < n; ++i) { if (i) url += ","; url += set[i].icao; }
  }

  _inflight = true;
  _net->get(url, [this, key](int code, const String& body) {
    _inflight = false;
    if (code == 200 && parse(body)) {
      _cache->put(key.c_str(), body, code, (uint32_t)time(nullptr));
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
  e["icaoId"] = e["altim"] = e["lat"] = e["lon"] = e["wdir"] = e["wspd"] = true;   // coords + wind from feed
  JsonObject c = e["clouds"].add<JsonObject>();
  c["cover"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) return false;

  std::vector<PressurePt> out;
  for (JsonObject o : arr) {
    if (!o["altim"].is<float>() || !o["lat"].is<float>()) continue;
    PressurePt p;
    p.icao = (const char*)(o["icaoId"] | "");
    p.hpa = (int)lround((float)o["altim"]);
    p.lat = (float)o["lat"]; p.lon = (float)o["lon"];
    p.wdir = o["wdir"].is<int>() ? (int)o["wdir"] : -1;
    p.wspd = o["wspd"].is<int>() ? (int)o["wspd"] : -1;
    int cl = 0;
    for (JsonObject c2 : o["clouds"].as<JsonArray>()) { int v = coverPct((const char*)(c2["cover"] | "")); if (v > cl) cl = v; }
    p.cloud = cl;
    MetarRec r; r.icao = p.icao; r.lat = p.lat; r.lon = p.lon; r.hpa = p.hpa;   // feed the shared pool
    r.cloud = p.cloud; r.wdir = p.wdir; r.wspd = p.wspd; r.fetchedAt = (uint32_t)time(nullptr);
    MetarStore::instance().upsert(r);
    out.push_back(p);
    if (out.size() >= 48) break;                    // cap to bound heap on dense regions
  }
  if (out.empty()) return false;
  _pts = std::move(out);
  return true;
}
