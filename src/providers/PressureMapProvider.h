#pragma once
#include <Arduino.h>
#include <vector>
#include "TleProvider.h"   // ProviderStatus

class Settings;
class NetClient;
class Cache;
class LocationService;

// providers/PressureMapProvider — a makeshift synoptic map from major-airport METARs
// (one aviationweather.gov request for a fixed station spread). We can't get a real
// WPC fronts/H-L product cheaply, but plotting airport sea-level pressure + cloud
// across a region gives a recognisable high/low pattern. The station set is chosen
// by the observer's location: continental-US set when in the US bbox, else a global
// spread. Coordinates come from the built-in table (the feed need only return altim
// + cloud), so the body stays small.
struct PressurePt {
  String icao;
  float  lat = 0, lon = 0;
  int    hpa = -1;       // sea-level/altimeter pressure (hPa)
  int    cloud = -1;     // 0..100 from the max cloud layer
};

class PressureMapProvider {
public:
  void begin(Settings* s, NetClient* net, Cache* cache, LocationService* loc);
  void refresh(bool force = false);

  const std::vector<PressurePt>& points() const { return _pts; }
  bool worldwide() const { return _world; }     // true = global set, false = US set
  ProviderStatus status() const { return _status; }
  uint32_t lastFetched() const { return _lastFetched; }

private:
  bool parse(const String& body);

  Settings*  _s = nullptr;
  NetClient* _net = nullptr;
  Cache*     _cache = nullptr;
  LocationService* _loc = nullptr;

  std::vector<PressurePt> _pts;
  bool _world = false;
  ProviderStatus _status = ProviderStatus::Loading;
  uint32_t _lastFetched = 0;
  bool _inflight = false;
};
