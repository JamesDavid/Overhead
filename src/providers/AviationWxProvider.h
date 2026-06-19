#pragma once
#include <Arduino.h>
#include <vector>
#include <time.h>
#include "TleProvider.h"   // ProviderStatus

class Settings;
class NetClient;
class Cache;
class EventBus;
class LocationService;

// providers/AviationWxProvider — nearby METAR/TAF (spec §14 aviation candidate).
// NOAA Aviation Weather Center data API (no key) bbox query around the observer,
// so no bundled airport dataset is needed; stations sorted by distance. Phase 1
// of the aviation-weather tab; soundings/Skew-T are a follow-up (BACKLOG).
struct Metar {
  String icao, name, raw, taf, wx;
  double lat = 0, lon = 0;
  float  distNm = 0;
  int    tempC = -999, dewpC = -999, wdir = -1, wspd = -1, altimHpa = -1;
  float  visSm = -1;
  int    ceilingFt = -1;        // lowest BKN/OVC base; -1 = none/clear
  int    cloud = -1;            // max cloud cover % (FEW 20 / SCT 40 / BKN 75 / OVC 100)
  time_t obsTime = 0;           // observation time (epoch UTC)
  String cat;                   // VFR | MVFR | IFR | LIFR
};

class AviationWxProvider {
public:
  void begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus, LocationService* loc);
  void refresh(bool force = false);

  const std::vector<Metar>& stations() const { return _stations; }
  bool hasSpeci() const { for (auto& m : _stations) if (m.raw.startsWith("SPECI")) return true; return false; }
  ProviderStatus status() const { return _status; }
  uint32_t lastFetched() const { return _lastFetched; }

private:
  void fetchMetars();
  void fetchTafs();
  bool parseMetars(const String& body);
  void attachTafs(const String& body);

  Settings*        _s = nullptr;
  NetClient*       _net = nullptr;
  Cache*           _cache = nullptr;
  EventBus*        _bus = nullptr;
  LocationService* _loc = nullptr;

  std::vector<Metar> _stations;
  ProviderStatus _status = ProviderStatus::Loading;
  uint32_t _lastFetched = 0;
  bool _inflight = false;
};
