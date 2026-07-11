#include "HazardProvider.h"
#include "../services/Settings.h"
#include "../services/NetClient.h"
#include "../services/Cache.h"
#include "../services/LocationService.h"
#include "../core/EventBus.h"
#include <ArduinoJson.h>
#include <time.h>

// AWC hazard codes -> plain phrases.
static String hazPhrase(String c) {
  c.toUpperCase();
  if (c.indexOf("CONV") >= 0 || c == "TS")              return "Thunderstorms";
  if (c.indexOf("TURB") >= 0)                            return "Turbulence";
  if (c.indexOf("ICE") >= 0 || c.indexOf("ICING") >= 0)  return "Icing";
  if (c.indexOf("MT") >= 0 && c.indexOf("OBSC") >= 0)    return "Mtn obscuration";
  if (c.indexOf("IFR") >= 0)                             return "IFR cloud/vis";
  if (c.indexOf("ASH") >= 0)                             return "Volcanic ash";
  if (c.indexOf("LLWS") >= 0 || c.indexOf("SHEAR") >= 0) return "Low-level wind shear";
  if (c.indexOf("WIND") >= 0)                            return "Strong surface wind";
  if (c.indexOf("OBSC") >= 0)                            return "Obscuration";
  return c.length() ? c : String("Hazard");
}
static String sevPhrase(String s) {
  s.toUpperCase();
  if (s.indexOf("SEV") >= 0) return "severe ";
  if (s.indexOf("MOD") >= 0) return "moderate ";
  if (s.indexOf("LGT") >= 0 || s.indexOf("LT ") >= 0) return "light ";
  return "";
}

void HazardProvider::begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus, LocationService* loc) {
  _s = s; _net = net; _cache = cache; _bus = bus; _loc = loc;
  refresh(false);
}

void HazardProvider::refresh(bool force) {
  if (!_loc->active().valid) return;
  if (!_inAir) fetchAirsig();
  if (!_inPi)  fetchPirep();
}

void HazardProvider::rebuild() {
  _all.clear();
  for (auto& h : _airsig) _all.push_back(h);
  for (auto& h : _pirep)  _all.push_back(h);
  _status = _all.empty() ? ProviderStatus::Ready : ProviderStatus::Ready;  // empty = no hazards (fine)
  if (_bus) _bus->publish(ProviderId::Weather);
}

void HazardProvider::fetchAirsig() {
  // Box the query server-side (like fetchPirep): the national feed is routinely
  // 100KB+ of polygon coords — too big to buffer on the no-PSRAM heap, and the
  // observer-proximity filter below discards almost all of it anyway.
  double la = _loc->active().lat, lo = _loc->active().lon;
  char url[160];
  snprintf(url, sizeof(url),
    "https://aviationweather.gov/api/data/airsigmet?format=json&bbox=%.1f,%.1f,%.1f,%.1f",
    la - 1.5, lo - 2.0, la + 1.5, lo + 2.0);
  _inAir = true;
  bool sent = _net->get(url, [this](int code, const String& body) {
    _inAir = false;
    if (code == 200) {
      JsonDocument filter;
      JsonObject e = filter.add<JsonObject>();
      e["airSigmetType"] = e["hazard"] = e["severity"] = e["altitudeLow1"] = e["altitudeHi1"] = true;
      e["rawAirSigmet"] = true;
      JsonObject c = e["coords"].add<JsonObject>(); c["lat"] = c["lon"] = true;
      JsonDocument doc;
      if (!deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
        // clear only on a good fetch+parse: a failed refresh must keep the
        // last-good hazards (serve stale), never fabricate an all-clear.
        _airsig.clear();
        double olat = _loc->active().lat, olon = _loc->active().lon;
        for (JsonObject o : doc.as<JsonArray>()) {
          JsonArray cs = o["coords"].as<JsonArray>();
          if (cs.isNull()) continue;
          double mnLa = 1e9, mxLa = -1e9, mnLo = 1e9, mxLo = -1e9;
          for (JsonObject p : cs) {
            double la = p["lat"] | 0.0, lo = p["lon"] | 0.0;
            mnLa = min(mnLa, la); mxLa = max(mxLa, la); mnLo = min(mnLo, lo); mxLo = max(mxLo, lo);
          }
          if (olat < mnLa - 0.7 || olat > mxLa + 0.7 || olon < mnLo - 0.7 || olon > mxLo + 0.7) continue;
          Hazard h; h.pirep = false;
          String typ = (const char*)(o["airSigmetType"] | "AIRMET");
          int lo = o["altitudeLow1"] | 0, hi = o["altitudeHi1"] | 0;
          String alt = (lo || hi) ? "  " + String(lo) + "-" + String(hi) + "ft" : "";
          String hdr = sevPhrase((const char*)(o["severity"] | "")) + hazPhrase((const char*)(o["hazard"] | ""))
                     + alt + "  (" + typ + ")";
          String raw = (const char*)(o["rawAirSigmet"] | "");
          raw.replace("\n", " ");
          h.text = raw.length() ? hdr + " - " + raw.substring(0, 70) : hdr;
          _airsig.push_back(h);
          if (_airsig.size() >= 8) break;
        }
      }
      _lastFetched = (uint32_t)time(nullptr);
    }
    rebuild();
  });
  if (!sent) _inAir = false;   // queue full -> retry next cycle instead of wedging
}

void HazardProvider::fetchPirep() {
  double la = _loc->active().lat, lo = _loc->active().lon;
  char url[150];
  snprintf(url, sizeof(url),
    "https://aviationweather.gov/api/data/pirep?format=json&bbox=%.1f,%.1f,%.1f,%.1f",
    la - 1.5, lo - 2.0, la + 1.5, lo + 2.0);
  _inPi = true;
  bool sent = _net->get(url, [this](int code, const String& body) {
    _inPi = false;
    if (code == 200) {
      JsonDocument filter;
      JsonObject e = filter.add<JsonObject>();
      e["rawOb"] = true;
      JsonDocument doc;
      if (!deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
        _pirep.clear();                          // last-good kept on any failure
        for (JsonObject o : doc.as<JsonArray>()) {
          String raw = (const char*)(o["rawOb"] | "");
          if (!raw.length()) continue;
          Hazard h; h.pirep = true; h.text = raw.substring(0, 40);
          _pirep.push_back(h);
          if (_pirep.size() >= 6) break;
        }
      }
    }
    rebuild();
  });
  if (!sent) _inPi = false;    // queue full -> retry next cycle instead of wedging
}
