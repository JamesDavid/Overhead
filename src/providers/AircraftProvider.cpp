#include "AircraftProvider.h"
#include "../services/Settings.h"
#include "../services/NetClient.h"
#include "../services/LocationService.h"
#include "../core/EventBus.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <math.h>
#include <time.h>

static constexpr double DEG = 3.14159265358979323846 / 180.0;

// Great-circle range (nm) + initial bearing (deg from North) observer->target.
static void relPos(double lat1, double lon1, double lat2, double lon2,
                   float& distNm, float& brgDeg) {
  double dlat = (lat2 - lat1) * DEG, dlon = (lon2 - lon1) * DEG;
  double a = sin(dlat / 2) * sin(dlat / 2)
           + cos(lat1 * DEG) * cos(lat2 * DEG) * sin(dlon / 2) * sin(dlon / 2);
  distNm = (float)(3440.065 * 2 * atan2(sqrt(a), sqrt(1 - a)));
  double y = sin(dlon) * cos(lat2 * DEG);
  double x = cos(lat1 * DEG) * sin(lat2 * DEG) - sin(lat1 * DEG) * cos(lat2 * DEG) * cos(dlon);
  double b = atan2(y, x) / DEG;
  brgDeg = (float)(b < 0 ? b + 360 : b);
}

void AircraftProvider::begin(Settings* s, NetClient* net, EventBus* bus, LocationService* loc) {
  _s = s; _net = net; _bus = bus; _loc = loc;
}

double AircraftProvider::centerLat() const { return _ctrSet ? _ctrLat : _loc->active().lat; }
double AircraftProvider::centerLon() const { return _ctrSet ? _ctrLon : _loc->active().lon; }

void AircraftProvider::poll() {
  if (_inflight || !_loc->active().valid) return;
  // Drop a contact set that hasn't refreshed in >60s. Two reasons: (1) don't show
  // 30-min-old traffic as if it were current; (2) free its heap — 24 String-heavy
  // contacts can pin the largest free block under the ~42 KB TLS floor, which makes
  // NetClient SKIP the very fetch meant to refresh them (a starvation loop). Releasing
  // them lifts the heap back over the floor so the next fetch can run.
  if (!_ac.empty() && _lastFetched && (uint32_t)time(nullptr) - _lastFetched > 60) {
    _ac.clear(); _ac.shrink_to_fit();
    _status = ProviderStatus::Loading;
    if (_bus) _bus->publish(ProviderId::Aircraft);
  }
  // Throttle when the radar isn't on screen (the scheduler still calls every few
  // seconds, but there's no point fetching/fragmenting the heap if nobody's looking).
  uint32_t nowMs = millis();
  if (_lastPollMs && nowMs - _lastPollMs < (_fg ? 0u : 60000u)) return;
  _local = _s->getString("adsbMode", "cloud") == "local";
  _radiusNm = (float)_s->getInt("adsbRadiusNm", 50);
  _hideGround = _s->getInt("adsbHideGround", 0) != 0;

  String url;
  if (_local) {
    String host = _s->getString("adsbHost", "");
    if (!host.length()) { _status = ProviderStatus::Error; return; }
    url = "http://" + host + "/data/aircraft.json";
  } else {
    char b[96];
    snprintf(b, sizeof(b), "https://api.airplanes.live/v2/point/%.4f/%.4f/%d",
             centerLat(), centerLon(), (int)_radiusNm);
    url = b;
  }

  _inflight = true;
  bool sent = _net->get(url, [this](int code, const String& body) {
    _inflight = false;
    if (code == 200 && body.length() > 2) {
      parse(body);
      _lastFetched = (uint32_t)time(nullptr);
      _status = ProviderStatus::Ready;
    } else {
      // code -3 = heap-floor TLS skip (most common cause of long staleness),
      // 429 = rate limited, <0 = connect/timeout. Keep the last contacts (Stale).
      _status = _ac.empty() ? ProviderStatus::Error : ProviderStatus::Stale;
      Serial.printf("[adsb] poll code=%d len=%u (keep %u, stale)\n", code, (unsigned)body.length(), (unsigned)_ac.size());
    }
    if (_bus) _bus->publish(ProviderId::Aircraft);
  });
  if (sent) _lastPollMs = nowMs;
  else      _inflight = false;           // req queue full; retry on the next poll
                                         // (otherwise _inflight sticks -> "scanning" forever)
}

void AircraftProvider::parse(const String& body) {
  // Filter the (potentially large) feed down to the fields we use. Accept both
  // "ac" (airplanes.live) and "aircraft" (tar1090/readsb) array keys.
  JsonDocument filter;
  for (const char* key : {"ac", "aircraft"}) {
    JsonObject e = filter[key].add<JsonObject>();
    e["hex"] = e["flight"] = e["lat"] = e["lon"] = e["alt_baro"] = true;
    e["gs"] = e["track"] = e["squawk"] = e["category"] = e["seen"] = e["t"] = true;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return;

  JsonArray arr = doc["ac"].as<JsonArray>();
  if (arr.isNull()) arr = doc["aircraft"].as<JsonArray>();
  if (arr.isNull()) return;

  double olat = centerLat(), olon = centerLon();
  int maxAlt = (int)_s->getInt("adsbMaxAltFt", 0);

  std::vector<Aircraft> out;
  for (JsonObject o : arr) {
    if (!o["lat"].is<double>() || !o["lon"].is<double>()) continue;
    Aircraft a;
    a.hex   = (const char*)(o["hex"] | "");
    a.flight= (const char*)(o["flight"] | "");
    a.flight.trim();
    a.lat   = o["lat"] | 0.0;
    a.lon   = o["lon"] | 0.0;
    JsonVariant alt = o["alt_baro"];
    if (alt.is<const char*>()) { a.onGround = String((const char*)alt) == "ground"; a.altFt = 0; }
    else                       { a.altFt = alt | 0.0f; }
    a.gsKt    = o["gs"] | 0.0f;
    a.trackDeg= o["track"] | 0.0f;
    a.squawk  = (const char*)(o["squawk"] | "");
    a.category= (const char*)(o["category"] | "");
    a.type    = (const char*)(o["t"] | "");
    a.seenS   = o["seen"] | 0.0f;
    if (a.seenS > 60) continue;                            // stale contact
    if (_hideGround && a.onGround) continue;               // ground-traffic filter
    if (maxAlt > 0 && a.altFt > maxAlt) continue;          // altitude filter
    relPos(olat, olon, a.lat, a.lon, a.distNm, a.bearingDeg);
    if (a.distNm > _radiusNm * 1.2f) continue;
    out.push_back(a);
  }
  std::sort(out.begin(), out.end(), [](const Aircraft& a, const Aircraft& b) { return a.distNm < b.distNm; });
  if (out.size() > 24) out.resize(24);   // bound RAM on no-PSRAM boards
  _ac = std::move(out);
}
