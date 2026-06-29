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

// Watchlist match: case-insensitive CONTAINS, because Celestrak puts the popular
// designation mid-name, e.g. "SAUDISAT 1C (SO-50)", "ISS (ZARYA)". startsWith()
// missed these. (AO-91's catalog name is "RADFXSAT (FOX-1B)" — use FOX-1B/RADFXSAT.)
inline bool satNameMatches(const String& name, const String& term) {
  if (!term.length()) return false;
  String n = name; n.toUpperCase();
  String t = term; t.toUpperCase();
  return n.indexOf(t) >= 0;
}

enum class ProviderStatus { Loading, Ready, Stale, Error };

class TleProvider {
public:
  void begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus);
  void refresh(bool force = false);

  const std::vector<TleEntry>& sats() const { return _sats; }

  ProviderStatus status() const { return _status; }
  uint32_t       lastFetched() const { return _lastFetched; }
  bool           fetching() const { return _pendingFetches > 0; }   // any TLE group fetch in-flight

  static constexpr int kGroupCount = 3;   // amateur + stations + SatGus (by name)

private:
  void fetchGroup(int idx);
  void loadFromCache(int idx);
  void parseInto(std::vector<TleEntry>& target, const String& tleText);
  void rebuildMerged();
  bool keepName(const String& name, size_t kept) const;

  // RAM is tight on no-PSRAM ESP32: holding the whole amateur+stations list as
  // Strings (~18 KB) fragments the heap so TLS fetches fail. So we retain only
  // watchlisted birds (spec §13: passes are computed for the watchlist anyway);
  // with no watchlist we keep up to kMaxKeep.
  std::vector<String> _watch;
  static constexpr size_t kMaxKeep = 20;

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
