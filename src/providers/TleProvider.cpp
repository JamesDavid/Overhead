#include "TleProvider.h"
#include "../services/Settings.h"
#include "../services/NetClient.h"
#include "../services/Cache.h"
#include "../core/EventBus.h"
#include <time.h>

// query = the gp.php selector (GROUP=... or NAME=...). SatGus (CrunchLabs, NORAD
// 62713) isn't in the amateur/stations groups, so fetch it by name.
struct GroupDef { const char* query; const char* key; };
static const GroupDef kGroups[TleProvider::kGroupCount] = {
  { "GROUP=amateur",  "tle_amateur"  },
  { "GROUP=stations", "tle_stations" },
  { "NAME=SATGUS",    "tle_satgus"   },
};

void TleProvider::begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus) {
  _s = s; _net = net; _cache = cache; _bus = bus;

  JsonArray wl = _s->doc()["watchlist"].as<JsonArray>();   // retain only watchlist (RAM)
  for (JsonVariant v : wl) { String p = (const char*)(v | ""); if (p.length()) _watch.push_back(p); }

  for (int i = 0; i < kGroupCount; ++i) loadFromCache(i);   // last-good, offline-friendly
  rebuildMerged();
  if (!_sats.empty()) _status = ProviderStatus::Stale;
  refresh(false);
}

void TleProvider::refresh(bool force) {
  uint32_t ttl = (uint32_t)_s->getInt("refreshTleHour", 12) * 3600UL;
  uint32_t now = (uint32_t)time(nullptr);
  bool clockValid = now > 1600000000UL;

  bool anyStale = false;
  for (int i = 0; i < kGroupCount; ++i) {
    CacheMeta m = _cache->stat(kGroups[i].key);
    // Trust a present cache until the clock is valid — don't force a boot fetch
    // just because NTP hasn't landed yet (that fetch can httpsSkip-fail and flip a
    // fresh cache to Stale). Re-checked properly on the Time-sync event.
    bool stale = force || !m.found || (clockValid && (now - m.fetchedAt) > ttl);
    if (stale) { anyStale = true; fetchGroup(i); }
  }
  // A fresh persisted cache is "ready" — don't mislabel it Stale just because we
  // didn't need to fetch this session (the data survives reboots in LittleFS).
  if (!anyStale && !_sats.empty()) _status = ProviderStatus::Ready;
}

void TleProvider::fetchGroup(int idx) {
  String url = String("https://celestrak.org/NORAD/elements/gp.php?")
             + kGroups[idx].query + "&FORMAT=tle";
  if (_sats.empty()) _status = ProviderStatus::Loading;
  _pendingFetches++;

  _net->get(url, [this, idx](int code, const String& body) {
    _pendingFetches--;
    bool ok = (code == 200) && body.length() > 60 && body.indexOf("1 ") >= 0;
    if (ok) {
      parseInto(_groupSats[idx], body);                       // parse the fetched body directly
      _cache->put(kGroups[idx].key, body, code, (uint32_t)time(nullptr));
      _lastFetched = (uint32_t)time(nullptr);
    } else {
      Serial.printf("[tle] fetch %s failed: code=%d len=%u\n",
                    kGroups[idx].key, code, (unsigned)body.length());
    }

    rebuildMerged();
    if (_sats.empty())            _status = ProviderStatus::Error;
    else if (_pendingFetches > 0) _status = ProviderStatus::Loading;
    else                          _status = ok ? ProviderStatus::Ready : ProviderStatus::Stale;

    Serial.printf("[tle] %u sats (%s)\n", (unsigned)_sats.size(),
                  _status == ProviderStatus::Ready ? "ready" : "stale/err");
    if (_bus) _bus->publish(ProviderId::Tle);
  });
}

void TleProvider::loadFromCache(int idx) {
  String body; CacheMeta m;
  if (_cache->get(kGroups[idx].key, body, m) && body.length()) {
    parseInto(_groupSats[idx], body);
    if (m.fetchedAt > _lastFetched) _lastFetched = m.fetchedAt;   // persist freshness across reboots
  }
}

bool TleProvider::keepName(const String& name, size_t kept) const {
  if (_watch.empty()) return kept < kMaxKeep;        // no watchlist: cap to bound RAM
  for (const auto& p : _watch) if (satNameMatches(name, p)) return true;
  return false;
}

void TleProvider::rebuildMerged() {
  _sats.clear();
  for (int i = 0; i < kGroupCount; ++i)
    for (auto& e : _groupSats[i]) _sats.push_back(e);
}

void TleProvider::parseInto(std::vector<TleEntry>& target, const String& tleText) {
  target.clear();
  int i = 0, n = tleText.length();
  String prev;
  while (i < n) {
    int eol = tleText.indexOf('\n', i);
    if (eol < 0) eol = n;
    String line = tleText.substring(i, eol);
    line.trim();
    i = eol + 1;
    if (line.startsWith("1 ") && line.length() >= 60) {
      String l1 = line;
      int eol2 = tleText.indexOf('\n', i);
      if (eol2 < 0) eol2 = n;
      String l2 = tleText.substring(i, eol2);
      l2.trim();
      i = eol2 + 1;
      if (l2.startsWith("2 ")) {
        String nm = prev.length() ? prev : String("UNNAMED");
        if (keepName(nm, target.size())) target.push_back({ nm, l1, l2 });
      }
    } else if (line.length()) {
      prev = line;
    }
  }
}

int TleProvider::indexOfPrefix(const String& namePrefix) const {
  for (size_t i = 0; i < _sats.size(); ++i)
    if (satNameMatches(_sats[i].name, namePrefix)) return (int)i;
  return -1;
}

const TleEntry* TleProvider::findByPrefix(const String& namePrefix) const {
  int idx = indexOfPrefix(namePrefix);
  return idx >= 0 ? &_sats[idx] : nullptr;
}
