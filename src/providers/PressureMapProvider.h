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
  bool worldwide() const { return _scope == 2; }  // global spread
  bool regional()  const { return _scope == 0; }  // ~200mi box around the observer (default)
  int  scope()     const { return _scope; }        // 0 regional, 1 US, 2 world
  void setScope(int s);                            // change scope + refetch
  void bbox(double& w0, double& w1, double& a0, double& a1) const { w0=_w0; w1=_w1; a0=_a0; a1=_a1; }
  ProviderStatus status() const { return _status; }
  uint32_t lastFetched() const { return _lastFetched; }

private:
  bool parse(const String& body);
  void computeBbox();                             // set _w0.._a1 for the current scope

  Settings*  _s = nullptr;
  NetClient* _net = nullptr;
  Cache*     _cache = nullptr;
  LocationService* _loc = nullptr;

  std::vector<PressurePt> _pts;
  int  _scope = 0;                                // 0 regional (default), 1 US, 2 world
  double _w0 = -126, _w1 = -66, _a0 = 24, _a1 = 50;  // current map bbox (lon0,lon1,lat0,lat1)
  ProviderStatus _status = ProviderStatus::Loading;
  uint32_t _lastFetched = 0;
  bool _inflight = false;
};
