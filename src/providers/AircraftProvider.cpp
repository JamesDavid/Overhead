#include "AircraftProvider.h"
#include "../services/Settings.h"
#include "../services/NetClient.h"
#include "../services/LocationService.h"
#include "../core/EventBus.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

bool AircraftProvider::overhead(String& msg, float nm, float elMin) const {
  bool found = false; float bestEl = elMin;
  for (const auto& a : _ac) {
    if (a.onGround || a.distNm <= 0 || a.distNm > nm || a.altFt <= 0) continue;
    float el = atan2f(a.altFt / 6076.12f, a.distNm) * 57.2957795f;   // elevation angle (deg)
    if (el >= bestEl) {                                              // highest overhead contact wins
      bestEl = el; found = true;
      msg = (a.flight.length() ? a.flight : a.hex) + "  " + String((int)a.altFt) + "ft  " + (int)el + "\xF7";
    }
  }
  return found;
}

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
    // Cloud feed over PLAIN HTTP (adsb.lol) so it dodges the ~42 KB contiguous-TLS
    // floor that makes the HTTPS sources httpsSkip-fail on the no-PSRAM board — the
    // radar then populates even with the web server up. Same {"ac":[...]} schema as
    // airplanes.live, so parse() is unchanged. (airplanes.live 301-redirects http->
    // https, so it can't be used without TLS.)
    char b[96];
    snprintf(b, sizeof(b), "http://api.adsb.lol/v2/point/%.4f/%.4f/%d",
             centerLat(), centerLon(), (int)_radiusNm);
    url = b;
  }

  _inflight = true;
  _staged = false;
  bool sent = _net->get(url,
    [this](int code, const String&) {            // UI thread: commit the staged parse
      _inflight = false;
      if (code == 200 && _staged) {
        _ac = std::move(_acStaging); _acStaging.clear(); _staged = false;
        _lastFetched = (uint32_t)time(nullptr);
        _status = ProviderStatus::Ready;
      } else {
        // code -3 = heap-floor TLS skip, 429 = rate limited, <0 = connect/timeout,
        // or a parse miss. Keep the last contacts (Stale).
        _status = _ac.empty() ? ProviderStatus::Error : ProviderStatus::Stale;
        Serial.printf("[adsb] poll code=%d staged=%d (keep %u)\n", code, (int)_staged, (unsigned)_ac.size());
      }
      if (_bus) _bus->publish(ProviderId::Aircraft);
    },
    [this](int code, Stream& s) {                // NET TASK: stream-filter into staging
      if (code == 200) parseStream(s);
    });
  if (sent) _lastPollMs = nowMs;
  else      _inflight = false;           // req queue full; retry on the next poll
                                         // (otherwise _inflight sticks -> "scanning" forever)
}

void AircraftProvider::parseStream(Stream& body) {
  // Runs on the NET TASK. Parse the aircraft array ELEMENT-BY-ELEMENT straight off
  // the HTTP stream so peak heap is a single aircraft object — NOT the whole feed.
  //
  // The old path built the entire filtered feed in one JsonDocument before the kMax
  // cap loop ran, so a busy metro (100-200 contacts on adsb.lol at 50 nm) needed a
  // large growing contiguous alloc. Because the aircraft feed is plain HTTP it also
  // BYPASSES NetClient's 42 KB TLS heap floor, so that big parse was the one thing
  // allowed to run when the heap was already critically low -> exhaust/fragment ->
  // heap-corruption panic (SW_CPU_RESET, "No core dump found") -> boot loop.

  // The aircraft feed skips NetClient's heap-floor guard (plain HTTP), so gate the
  // parse here: if the largest free block is too small, bail and keep the last
  // (stale) contacts rather than risk a corrupting allocation.
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 20000) {
    _staged = false;
    return;
  }

  // Advance the stream to the start of the aircraft array. adsb.lol / airplanes.live
  // use "ac"; tar1090 / readsb use "aircraft". "ac" is a prefix of "aircraft", so
  // find("\"ac") lands on either key; then walk to the array's opening '['.
  if (!body.find((char*)"\"ac")) return;
  if (!body.find((char*)"["))   return;

  // Per-ELEMENT filter: only the fields we actually use. Applied to each object as
  // it is parsed, so the reused `elem` document stays tiny.
  JsonDocument filter;
  filter["hex"] = filter["flight"] = filter["lat"] = filter["lon"] = true;
  filter["alt_baro"] = filter["gs"] = filter["track"] = filter["squawk"] = true;
  filter["category"] = filter["seen"] = filter["t"] = true;
  filter["baro_rate"] = filter["geom_rate"] = true;   // vertical rate (ft/min) for climb/descent trend

  double olat = centerLat(), olon = centerLon();
  int    maxAlt = (int)_s->getInt("adsbMaxAltFt", 0);

  // Keep only the nearest kMax. Reserved once at the cap, `out` stays a small fixed
  // allocation. Larger-screen/heap boards (CrowPanel) keep more so traffic out toward
  // the ring edge shows, not just the nearest handful.
#if defined(BOARD_CROWPANEL_S3_5HMI)
  static constexpr int kMax = 48;
#else
  static constexpr int kMax = 24;
#endif
  std::vector<Aircraft> out; out.reserve(kMax + 1);

  // This whole read+parse runs on the NetTask, pinned to core 0 alongside the WiFi
  // task, at a priority just above the idle task. If it holds core 0 for longer than
  // the ~5 s task-watchdog window, IDLE0 never runs and the TWDT aborts (that's the
  // "IDLE0 did not reset" crash). So we (a) yield briefly every few contacts to let
  // IDLE0 run and pet the watchdog, and (b) cap the total parse time so a huge/slow
  // feed can never monopolise the core — we just keep whatever we gathered so far.
  static constexpr uint32_t kMaxParseMs = 2500;
  const uint32_t parseStart = millis();
  int scanned = 0;

  // Parse one array element at a time. `elem` is reused each iteration, so peak heap
  // is a single filtered aircraft object regardless of how busy the feed is.
  JsonDocument elem;
  do {
    // Give IDLE0 a slice every 8 contacts so the task watchdog stays fed.
    if ((++scanned & 0x07) == 0) vTaskDelay(1);
    if (millis() - parseStart > kMaxParseMs) break;        // time-box the parse

    elem.clear();
    DeserializationError err =
        deserializeJson(elem, body, DeserializationOption::Filter(filter));
    if (err) break;                                        // malformed / truncated -> stop

    JsonObject o = elem.as<JsonObject>();
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
    a.vsFpm   = o["baro_rate"].is<float>() ? (o["baro_rate"] | 0.0f) : (o["geom_rate"] | 0.0f);
    a.squawk  = (const char*)(o["squawk"] | "");
    a.category= (const char*)(o["category"] | "");
    a.type    = (const char*)(o["t"] | "");
    a.seenS   = o["seen"] | 0.0f;
    if (a.seenS > 60) continue;                            // stale contact
    if (_hideGround && a.onGround) continue;               // ground-traffic filter
    if (maxAlt > 0 && a.altFt > maxAlt) continue;          // altitude filter
    relPos(olat, olon, a.lat, a.lon, a.distNm, a.bearingDeg);
    if (a.distNm > _radiusNm * 1.2f) continue;
    if ((int)out.size() < kMax) { out.push_back(a); continue; }
    int far = 0;                                           // else replace the farthest kept, if closer
    for (int k = 1; k < (int)out.size(); ++k) if (out[k].distNm > out[far].distNm) far = k;
    if (a.distNm < out[far].distNm) out[far] = a;

    // findUntil consumes the delimiter after each element: "," -> continue,
    // "]" (or EOF) -> returns false and ends the array loop.
  } while (body.findUntil((char*)",", (char*)"]"));

  std::sort(out.begin(), out.end(), [](const Aircraft& a, const Aircraft& b) { return a.distNm < b.distNm; });
  _acStaging = std::move(out);
  _staged = true;                        // tell the UI-thread cb a good parse is ready
}
