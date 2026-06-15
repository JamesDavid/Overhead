#pragma once
#include <Arduino.h>
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
  String hex, flight, squawk, category;
  double lat = 0, lon = 0;
  float  altFt = 0, gsKt = 0, trackDeg = 0, seenS = 0;
  float  distNm = 0, bearingDeg = 0;
  bool   onGround = false;
};

class AircraftProvider {
public:
  void begin(Settings* s, NetClient* net, EventBus* bus, LocationService* loc);
  void poll();

  const std::vector<Aircraft>& aircraft() const { return _ac; }
  ProviderStatus status() const { return _status; }
  uint32_t       lastFetched() const { return _lastFetched; }
  bool           local() const { return _local; }
  float          radiusNm() const { return _radiusNm; }

private:
  void parse(const String& body);

  Settings*        _s = nullptr;
  NetClient*       _net = nullptr;
  EventBus*        _bus = nullptr;
  LocationService* _loc = nullptr;

  std::vector<Aircraft> _ac;
  ProviderStatus _status = ProviderStatus::Loading;
  uint32_t _lastFetched = 0;
  float    _radiusNm = 50;
  bool     _local = false;
  bool     _inflight = false;
};
