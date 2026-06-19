#include "AviationWxProvider.h"
#include "../services/Settings.h"
#include "../services/NetClient.h"
#include "../services/Cache.h"
#include "../services/MetarStore.h"
#include "../services/LocationService.h"
#include "../core/EventBus.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <math.h>
#include <time.h>

static constexpr double DEG = 3.14159265358979323846 / 180.0;

static float distNm(double lat1, double lon1, double lat2, double lon2) {
  double dlat = (lat2 - lat1) * DEG, dlon = (lon2 - lon1) * DEG;
  double a = sin(dlat / 2) * sin(dlat / 2)
           + cos(lat1 * DEG) * cos(lat2 * DEG) * sin(dlon / 2) * sin(dlon / 2);
  return (float)(3440.065 * 2 * atan2(sqrt(a), sqrt(1 - a)));
}

static String flightCat(int ceilFt, float visSm) {
  bool haveCeil = ceilFt >= 0;
  if ((haveCeil && ceilFt < 500)  || (visSm >= 0 && visSm < 1)) return "LIFR";
  if ((haveCeil && ceilFt < 1000) || (visSm >= 0 && visSm < 3)) return "IFR";
  if ((haveCeil && ceilFt < 3000) || (visSm >= 0 && visSm < 5)) return "MVFR";
  return "VFR";
}

void AviationWxProvider::begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus, LocationService* loc) {
  _s = s; _net = net; _cache = cache; _bus = bus; _loc = loc;
  String body; CacheMeta m;                         // restore last-good METARs (offline-friendly)
  if (_cache->get("avwx_metar", body, m) && parseMetars(body)) {
    _status = ProviderStatus::Stale;
    _lastFetched = m.fetchedAt;
  }
  refresh(false);
}

void AviationWxProvider::refresh(bool force) {
  if (_inflight || !_loc->active().valid) return;
  uint32_t ttl = (uint32_t)_s->getInt("refreshAvWxMin", 12) * 60UL;
  uint32_t now = (uint32_t)time(nullptr);
  CacheMeta m = _cache->stat("avwx_metar");
  bool stale = force || !m.found || (now >= 1600000000UL && (now - m.fetchedAt) > ttl);  // trust cache pre-NTP
  if (stale) fetchMetars();
  else if (!_stations.empty()) _status = ProviderStatus::Ready;
}

void AviationWxProvider::fetchMetars() {
  double la = _loc->active().lat, lo = _loc->active().lon;
  char url[160];
  snprintf(url, sizeof(url),
    "https://aviationweather.gov/api/data/metar?format=json&bbox=%.2f,%.2f,%.2f,%.2f",
    la - 0.9, lo - 1.2, la + 0.9, lo + 1.2);
  if (_stations.empty()) _status = ProviderStatus::Loading;
  _inflight = true;
  bool sent = _net->get(url, [this](int code, const String& body) {
    _inflight = false;
    if (code == 200 && parseMetars(body)) {
      _cache->put("avwx_metar", body, code, (uint32_t)time(nullptr));
      _lastFetched = (uint32_t)time(nullptr);
      _status = ProviderStatus::Ready;
      fetchTafs();                       // then attach TAFs for the same box
    } else if (_stations.empty()) {
      _status = ProviderStatus::Error;
    }
    if (_bus) _bus->publish(ProviderId::Weather);
  });
  if (!sent) _inflight = false;          // req queue full; retry on the next refresh
}

void AviationWxProvider::fetchTafs() {
  double la = _loc->active().lat, lo = _loc->active().lon;
  char url[160];
  snprintf(url, sizeof(url),
    "https://aviationweather.gov/api/data/taf?format=json&bbox=%.2f,%.2f,%.2f,%.2f",
    la - 0.9, lo - 1.2, la + 0.9, lo + 1.2);
  _net->get(url, [this](int code, const String& body) {
    if (code == 200) attachTafs(body);
    if (_bus) _bus->publish(ProviderId::Weather);
  });
}

bool AviationWxProvider::parseMetars(const String& body) {
  JsonDocument filter;
  JsonObject e = filter.add<JsonObject>();
  e["icaoId"] = e["name"] = e["lat"] = e["lon"] = e["temp"] = e["dewp"] = true;
  e["wdir"] = e["wspd"] = e["visib"] = e["altim"] = e["wxString"] = e["rawOb"] = e["obsTime"] = true;
  JsonObject c = e["clouds"].add<JsonObject>();
  c["cover"] = c["base"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) return false;

  double olat = _loc->active().lat, olon = _loc->active().lon;
  std::vector<Metar> out;
  for (JsonObject o : arr) {
    Metar m;
    m.icao = (const char*)(o["icaoId"] | "");
    if (!m.icao.length()) continue;
    m.name = (const char*)(o["name"] | "");
    m.lat = o["lat"] | 0.0; m.lon = o["lon"] | 0.0;
    m.tempC = o["temp"].is<float>() ? (int)lround((float)o["temp"]) : -999;
    m.dewpC = o["dewp"].is<float>() ? (int)lround((float)o["dewp"]) : -999;
    m.wdir = o["wdir"].is<int>() ? (int)o["wdir"] : -1;
    m.wspd = o["wspd"].is<int>() ? (int)o["wspd"] : -1;
    m.altimHpa = o["altim"].is<float>() ? (int)lround((float)o["altim"]) : -1;
    m.visSm = atof(String((const char*)(o["visib"] | "-1")).c_str());
    m.wx = (const char*)(o["wxString"] | "");
    m.raw = (const char*)(o["rawOb"] | "");
    m.obsTime = (time_t)(o["obsTime"] | 0);
    int ceil = -1, cov = -1;
    for (JsonObject cl : o["clouds"].as<JsonArray>()) {
      String c = (const char*)(cl["cover"] | "");
      int pct = (c == "OVC" || c == "OVX") ? 100 : c == "BKN" ? 75 : c == "SCT" ? 40
              : c == "FEW" ? 20 : (c == "CLR" || c == "SKC" || c == "CAVOK" || c == "NSC") ? 0 : -1;
      if (pct > cov) cov = pct;
      if ((c == "BKN" || c == "OVC" || c == "OVX") && cl["base"].is<int>()) {
        int b = cl["base"]; if (ceil < 0 || b < ceil) ceil = b;
      }
    }
    m.ceilingFt = ceil; m.cloud = cov;
    m.cat = flightCat(ceil, m.visSm);
    m.distNm = distNm(olat, olon, m.lat, m.lon);
    MetarRec r; r.icao = m.icao; r.lat = m.lat; r.lon = m.lon; r.hpa = m.altimHpa;   // feed the shared pool
    r.cloud = m.cloud; r.wdir = m.wdir; r.wspd = m.wspd; r.tempC = m.tempC; r.cat = m.cat;
    r.obsTime = m.obsTime; r.fetchedAt = (uint32_t)time(nullptr);
    MetarStore::instance().upsert(r);
    out.push_back(m);
  }
  if (out.empty()) return false;
  std::sort(out.begin(), out.end(), [](const Metar& a, const Metar& b) { return a.distNm < b.distNm; });
  if (out.size() > 12) out.resize(12);   // bound RAM on no-PSRAM boards
  _stations = std::move(out);
  return true;
}

void AviationWxProvider::attachTafs(const String& body) {
  JsonDocument filter;
  JsonObject e = filter.add<JsonObject>();
  e["icaoId"] = e["rawTAF"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return;
  for (JsonObject o : doc.as<JsonArray>()) {
    String icao = (const char*)(o["icaoId"] | "");
    String taf = (const char*)(o["rawTAF"] | "");
    for (auto& m : _stations) if (m.icao == icao) { m.taf = taf; break; }
  }
}
