#pragma once
#include <Arduino.h>
#include "TleProvider.h"   // ProviderStatus

class Settings;
class NetClient;
class Cache;
class EventBus;

// providers/MarsProvider — current Mars-rover status from NASA's mars-photos API
// (one small request to /rovers: name/status/max_sol/max_date/total_photos). Light
// payload, low cadence (rovers update ~daily). Feeds the Missions page; the page
// also computes Mars distance/light-time/sols itself so it works without this feed.
struct RoverInfo {
  String name;
  String status;        // "active" | "complete"
  long   maxSol = -1;   // latest sol with data
  String maxDate;       // latest Earth date (YYYY-MM-DD)
  long   totalPhotos = -1;
};

class MarsProvider {
public:
  void begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus);
  void refresh(bool force = false);

  const RoverInfo& perseverance() const { return _persev; }
  const RoverInfo& curiosity() const { return _curio; }
  ProviderStatus status() const { return _status; }
  uint32_t lastFetched() const { return _lastFetched; }

private:
  bool parse(const String& body);

  Settings*  _s = nullptr;
  NetClient* _net = nullptr;
  Cache*     _cache = nullptr;
  EventBus*  _bus = nullptr;

  RoverInfo _persev, _curio;
  ProviderStatus _status = ProviderStatus::Loading;
  uint32_t _lastFetched = 0;
  bool _inflight = false;
};
