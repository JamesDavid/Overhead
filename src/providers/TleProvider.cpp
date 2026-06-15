#include "TleProvider.h"
#include "../services/Settings.h"
#include "../services/NetClient.h"
#include "../services/Cache.h"
#include "../core/EventBus.h"
#include <time.h>

struct GroupDef { const char* group; const char* key; };
static const GroupDef kGroups[TleProvider::kGroupCount] = {
  { "amateur",  "tle_amateur"  },
  { "stations", "tle_stations" },
};

void TleProvider::begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus) {
  _s = s; _net = net; _cache = cache; _bus = bus;

  for (int i = 0; i < kGroupCount; ++i) loadFromCache(i);   // last-good, offline-friendly
  rebuildMerged();
  if (!_sats.empty()) _status = ProviderStatus::Stale;
  refresh(false);
}

void TleProvider::refresh(bool force) {
  uint32_t ttl = (uint32_t)_s->getInt("refreshTleHour", 12) * 3600UL;
  uint32_t now = (uint32_t)time(nullptr);
  bool clockValid = now > 1600000000UL;

  for (int i = 0; i < kGroupCount; ++i) {
    CacheMeta m = _cache->stat(kGroups[i].key);
    bool stale = force || !m.found || !clockValid || (now - m.fetchedAt) > ttl;
    if (stale) fetchGroup(i);
  }
}

void TleProvider::fetchGroup(int idx) {
  String url = String("https://celestrak.org/NORAD/elements/gp.php?GROUP=")
             + kGroups[idx].group + "&FORMAT=tle";
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
  if (_cache->get(kGroups[idx].key, body, m) && body.length())
    parseInto(_groupSats[idx], body);
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
      if (l2.startsWith("2 "))
        target.push_back({ prev.length() ? prev : String("UNNAMED"), l1, l2 });
    } else if (line.length()) {
      prev = line;
    }
  }
}

int TleProvider::indexOfPrefix(const String& namePrefix) const {
  for (size_t i = 0; i < _sats.size(); ++i)
    if (_sats[i].name.startsWith(namePrefix)) return (int)i;
  return -1;
}

const TleEntry* TleProvider::findByPrefix(const String& namePrefix) const {
  int idx = indexOfPrefix(namePrefix);
  return idx >= 0 ? &_sats[idx] : nullptr;
}
