#pragma once
#include <Arduino.h>
#include <vector>

class Settings;
class NetClient;
class Cache;
class EventBus;

// providers/TleProvider — Celestrak GP TLE source (spec §6 Satellites, §9).
// Fetches the configured GP groups (amateur + stations) via the NetTask, caches
// the raw text in LittleFS (offline survival), and parses into a flat list.
// Parsing works off the freshly-fetched body directly, so a live fetch still
// populates the list even if the cache write fails. Refreshes on a long TTL;
// publishes ProviderId::Tle when the list changes.
struct TleEntry {
  String name;
  String line1;
  String line2;
};

enum class ProviderStatus { Loading, Ready, Stale, Error };

class TleProvider {
public:
  void begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus);
  void refresh(bool force = false);

  const std::vector<TleEntry>& sats() const { return _sats; }
  const TleEntry* findByPrefix(const String& namePrefix) const;
  int indexOfPrefix(const String& namePrefix) const;

  ProviderStatus status() const { return _status; }
  uint32_t       lastFetched() const { return _lastFetched; }

  static constexpr int kGroupCount = 2;   // amateur + stations

private:
  void fetchGroup(int idx);
  void loadFromCache(int idx);
  void parseInto(std::vector<TleEntry>& target, const String& tleText);
  void rebuildMerged();

  Settings*  _s     = nullptr;
  NetClient* _net   = nullptr;
  Cache*     _cache = nullptr;
  EventBus*  _bus   = nullptr;

  std::vector<TleEntry> _groupSats[kGroupCount];
  std::vector<TleEntry> _sats;             // merged view
  ProviderStatus _status      = ProviderStatus::Loading;
  uint32_t       _lastFetched = 0;
  int            _pendingFetches = 0;
};
