#pragma once
#include <Arduino.h>
#include <Stream.h>
#include <vector>
#include "TleProvider.h"   // ProviderStatus

class Settings;
class NetClient;
class EventBus;
class LocationService;

// providers/AircraftProvider — nearby ADS-B aircraft (spec §6 Aircraft, §9).
// Two runtime modes: a local readsb/tar1090 feeder (http://<host>/data/aircraft.json,
// no limits) or the airplanes.live cloud point query (1 req/s). Computes range +
// bearing from the observer, filters/sorts by range, drops stale contacts.
// Polled by the Scheduler (never on the UI tick).
struct Aircraft {
  String hex, flight, squawk, category, type;   // type = ICAO type code (e.g. B738)
  double lat = 0, lon = 0;
  float  altFt = 0, gsKt = 0, trackDeg = 0, seenS = 0;
  float  vsFpm = 0;                 // vertical rate (ft/min): >0 climbing, <0 descending, ~0 level
  float  distNm = 0, bearingDeg = 0;
  bool   onGround = false;
};

class AircraftProvider {
public:
  void begin(Settings* s, NetClient* net, EventBus* bus, LocationService* loc);
  void poll();
  // Foreground = Aircraft page is showing -> poll at the scheduler's full rate.
  // Background -> throttle to ~60 s (radar isn't visible; saves requests + heap).
  void setForeground(bool fg) { _fg = fg; }

  // Recenter the query/radar on an arbitrary point (e.g. a nearby airport) instead
  // of the observer. Range/bearing are computed from this centre. clearCenter()
  // returns to the observer's location.
  void setCenter(double lat, double lon) { _ctrLat = lat; _ctrLon = lon; _ctrSet = true; }
  void clearCenter() { _ctrSet = false; }
  bool centered() const { return _ctrSet; }

  const std::vector<Aircraft>& aircraft() const { return _ac; }
  ProviderStatus status() const { return _status; }
  uint32_t       lastFetched() const { return _lastFetched; }
  bool           local() const { return _local; }
  float          radiusNm() const { return _radiusNm; }
  bool           hideGround() const { return _hideGround; }
  // A contact passing (nearly) overhead: within `nm` horizontal AND above `elMin`
  // elevation angle (atan(alt / horizontal range)). For a cross-tab "plane overhead"
  // alert. msg <- "FLIGHT  ALTft" of the highest such contact.
  bool           overhead(String& msg, float nm = 5.0f, float elMin = 20.0f) const;

  // Any contact squawking a 75/76/7700 emergency code (for a cross-tab alert).
  bool           hasEmergency() const {
    for (const auto& a : _ac)
      if (a.squawk == "7700" || a.squawk == "7600" || a.squawk == "7500") return true;
    return false;
  }

private:
  void parseStream(Stream& body);   // runs on the NET TASK: filter-stream into _acStaging
  double centerLat() const;
  double centerLon() const;

  Settings*        _s = nullptr;
  NetClient*       _net = nullptr;
  EventBus*        _bus = nullptr;
  LocationService* _loc = nullptr;

  std::vector<Aircraft> _ac;          // committed list (UI thread reads this in draw)
  std::vector<Aircraft> _acStaging;   // net task parses into here; UI cb swaps -> _ac
  volatile bool _staged = false;      // net task set true after a good parse; UI commits
  ProviderStatus _status = ProviderStatus::Loading;
  uint32_t _lastFetched = 0;
  float    _radiusNm = 50;
  bool     _local = false;
  bool     _hideGround = false;
  bool     _inflight = false;
  bool     _fg = false;            // Aircraft page in the foreground
  uint32_t _lastPollMs = 0;        // millis() of the last issued fetch
  bool     _ctrSet = false;        // centre override active (else observer)
  double   _ctrLat = 0, _ctrLon = 0;
};
