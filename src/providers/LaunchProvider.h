#pragma once
#include <Arduino.h>
#include <vector>
#include "TleProvider.h"   // for ProviderStatus

class Settings;
class NetClient;
class Cache;
class EventBus;

// providers/LaunchProvider — upcoming rocket launches (spec §6 Launches, §9).
// Primary: Launch Library 2 (heavily throttled -> cache aggressively, serve stale
// on 429). Fallback: RocketLaunch.Live next/5 on LL2 error. Parses with an
// ArduinoJson filter to keep only the fields we render. Publishes Launch.
struct Launch {
  String name;
  String provider;
  String vehicle;
  String pad;
  String location;
  String mission;
  String statusName;
  String statusAbbrev;
  String netPrecision;     // "" = exact-ish; else e.g. "Hour", "Day", "Month"
  time_t net = 0;          // T-0 epoch (UTC); 0 if unknown
};

class LaunchProvider {
public:
  void begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus);
  void refresh(bool force = false);

  const std::vector<Launch>& launches() const { return _launches; }
  ProviderStatus status() const { return _status; }
  uint32_t       lastFetched() const { return _lastFetched; }
  bool           usingFallback() const { return _fallback; }
  bool           fetching() const { return _inflight; }   // a network fetch is queued/in-flight

private:
  void fetchLL2();
  void fetchFallback();
  // Status when a fetch fails/skips: keep the cache "Ready" while it's within TTL (a
  // skipped refresh shouldn't flip fresh data to "stale"), else Stale, else Error.
  ProviderStatus freshness() const;
  bool parseLL2(const String& body);   // returns true if it yielded launches
  bool parseRLL(const String& body);

  Settings*  _s = nullptr;
  NetClient* _net = nullptr;
  Cache*     _cache = nullptr;
  EventBus*  _bus = nullptr;

  std::vector<Launch> _launches;
  ProviderStatus _status = ProviderStatus::Loading;
  uint32_t       _lastFetched = 0;
  bool           _fallback = false;
  bool           _inflight = false;    // true while LL2/fallback request is queued or awaiting its callback
};
