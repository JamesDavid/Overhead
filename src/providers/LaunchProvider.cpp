#include "LaunchProvider.h"
#include "../services/Settings.h"
#include "../services/NetClient.h"
#include "../services/Cache.h"
#include "../core/EventBus.h"
#include <ArduinoJson.h>
#include <time.h>

static const char* kCacheKey = "launch_ll2";

// ISO-8601 "YYYY-MM-DDThh:mm:ssZ" -> unix UTC (no libc TZ dependence).
static time_t isoToEpoch(const String& iso) {
  int y, mo, d, h, mi, s;
  if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return 0;
  static const int cum[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  long days = (long)(y - 1970) * 365 + (y - 1969) / 4 - (y - 1901) / 100 + (y - 1601) / 400
            + cum[(mo - 1) % 12] + (d - 1);
  if (mo > 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) days += 1;
  return (time_t)days * 86400 + h * 3600 + mi * 60 + s;
}

void LaunchProvider::begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus) {
  _s = s; _net = net; _cache = cache; _bus = bus;

  String body; CacheMeta m;                       // last-good from cache first
  if (_cache->get(kCacheKey, body, m) && parseLL2(body)) {
    _status = ProviderStatus::Stale;
    _lastFetched = m.fetchedAt;                   // persist freshness across reboots
  }
  refresh(false);
}

void LaunchProvider::refresh(bool force) {
  uint32_t ttl = (uint32_t)_s->getInt("refreshLaunchMin", 45) * 60UL;
  uint32_t now = (uint32_t)time(nullptr);
  CacheMeta m = _cache->stat(kCacheKey);
  bool clockValid = now > 1600000000UL;
  bool stale = force || !m.found || (clockValid && (now - m.fetchedAt) > ttl);  // trust cache pre-NTP
  if (stale) fetchLL2();
  else if (!_launches.empty()) _status = ProviderStatus::Ready;   // fresh persisted cache is ready
}

ProviderStatus LaunchProvider::freshness() const {
  if (_launches.empty()) return ProviderStatus::Error;
  uint32_t ttl = (uint32_t)_s->getInt("refreshLaunchMin", 45) * 60UL, now = (uint32_t)time(nullptr);
  bool fresh = _lastFetched && now > 1600000000UL && (now - _lastFetched) <= ttl;
  return fresh ? ProviderStatus::Ready : ProviderStatus::Stale;   // within TTL -> still good
}

void LaunchProvider::fetchLL2() {
  if (_launches.empty()) _status = ProviderStatus::Loading;
  const char* url = "https://ll.thespacedevs.com/2.2.0/launch/upcoming/?limit=8&mode=list";
  _inflight = _net->get(url, [this](int code, const String& body) {
    if (code == 200 && parseLL2(body)) {
      _cache->put(kCacheKey, body, code, (uint32_t)time(nullptr));
      _lastFetched = (uint32_t)time(nullptr);
      _fallback = false;
      _status = ProviderStatus::Ready;
      _inflight = false;
      Serial.printf("[launch] LL2 ok: %u launches\n", (unsigned)_launches.size());
      if (_bus) _bus->publish(ProviderId::Launch);
    } else {
      Serial.printf("[launch] LL2 failed (code %d) -> fallback\n", code);
      // Keep fresh cache "Ready" (a skipped/failed refresh isn't staleness); try fallback.
      _status = freshness();
      fetchFallback();                                 // keeps _inflight asserted through the fallback
    }
  });
}

void LaunchProvider::fetchFallback() {
  const char* url = "https://fdo.rocketlaunch.live/json/launches/next/5";
  _inflight = _net->get(url, [this](int code, const String& body) {
    if (code == 200 && parseRLL(body)) {
      _lastFetched = (uint32_t)time(nullptr);
      _fallback = true;
      _status = ProviderStatus::Ready;
      Serial.printf("[launch] RLL fallback ok: %u launches\n", (unsigned)_launches.size());
    } else {
      _status = freshness();                          // within TTL stays Ready, not Stale
      Serial.printf("[launch] fallback failed (code %d)\n", code);
    }
    _inflight = false;
    if (_bus) _bus->publish(ProviderId::Launch);
  });
}

bool LaunchProvider::parseLL2(const String& body) {
  // LL2 mode=list FLATTENS most fields to strings: pad, location, lsp_name,
  // launcher, mission are strings (not the nested objects of detailed mode).
  // status + net_precision stay as small objects.
  JsonDocument filter;
  JsonObject r = filter["results"].add<JsonObject>();
  r["name"] = true;
  r["net"] = true;
  r["pad"] = true;
  r["location"] = true;          // launch site, e.g. "Cape Canaveral SFS, FL, USA"
  r["lsp_name"] = true;          // provider
  r["launcher"] = true;          // vehicle (string or object across versions)
  r["mission"] = true;           // mission (string or object)
  r["status"]["name"] = true;
  r["status"]["abbrev"] = true;
  r["net_precision"]["name"] = true;
  r["net_precision"]["abbrev"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  JsonArray results = doc["results"].as<JsonArray>();
  if (results.isNull()) return false;

  std::vector<Launch> out;
  for (JsonObject o : results) {
    Launch l;
    l.name         = (const char*)(o["name"] | "");
    l.net          = isoToEpoch((const char*)(o["net"] | ""));
    l.netPrecision = (const char*)(o["net_precision"]["name"] | "");
    l.statusName   = (const char*)(o["status"]["name"] | "");
    l.statusAbbrev = (const char*)(o["status"]["abbrev"] | "");
    l.provider     = (const char*)(o["lsp_name"] | "");
    l.pad          = (const char*)(o["pad"] | "");
    l.location     = (const char*)(o["location"] | "");
    // launcher/mission may be a flat string (list mode) or an object.
    JsonVariant lv = o["launcher"];
    if (lv.is<const char*>())      l.vehicle = (const char*)lv;
    else if (lv.is<JsonObject>())  l.vehicle = (const char*)(lv["full_name"] | lv["name"] | "");
    JsonVariant mv = o["mission"];
    if (mv.is<const char*>())      l.mission = (const char*)mv;
    else if (mv.is<JsonObject>())  l.mission = (const char*)(mv["name"] | "");
    if (l.name.length()) out.push_back(l);
  }
  if (out.empty()) return false;
  _launches = std::move(out);
  return true;
}

bool LaunchProvider::parseRLL(const String& body) {
  JsonDocument filter;
  JsonObject r = filter["result"].add<JsonObject>();
  r["name"] = true;
  r["t0"] = true;
  r["win_open"] = true;
  r["provider"]["name"] = true;
  r["vehicle"]["name"] = true;
  r["pad"]["name"] = true;
  r["pad"]["location"]["name"] = true;
  JsonObject mi = r["missions"].add<JsonObject>();
  mi["name"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  JsonArray result = doc["result"].as<JsonArray>();
  if (result.isNull()) return false;

  std::vector<Launch> out;
  for (JsonObject o : result) {
    Launch l;
    l.name     = (const char*)(o["name"] | "");
    String t0  = (const char*)(o["t0"] | "");
    if (!t0.length()) t0 = (const char*)(o["win_open"] | "");
    l.net      = isoToEpoch(t0);
    l.provider = (const char*)(o["provider"]["name"] | "");
    l.vehicle  = (const char*)(o["vehicle"]["name"] | "");
    l.pad      = (const char*)(o["pad"]["name"] | "");
    l.location = (const char*)(o["pad"]["location"]["name"] | "");
    JsonArray ms = o["missions"].as<JsonArray>();
    if (!ms.isNull() && ms.size()) l.mission = (const char*)(ms[0]["name"] | "");
    if (l.name.length()) out.push_back(l);
  }
  if (out.empty()) return false;
  _launches = std::move(out);
  return true;
}
