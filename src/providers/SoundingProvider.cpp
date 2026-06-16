#include "SoundingProvider.h"
#include "../services/Settings.h"
#include "../services/NetClient.h"
#include "../services/Cache.h"
#include "../services/LocationService.h"
#include "../core/EventBus.h"
#include <time.h>

void SoundingProvider::begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus, LocationService* loc) {
  _s = s; _net = net; _cache = cache; _bus = bus; _loc = loc;
  String body; CacheMeta m;
  if (_cache->get("sounding", body, m)) parse(body);
  refresh(false);
}

void SoundingProvider::refresh(bool force) {
  if (_inflight || !_loc->active().valid) return;
  uint32_t ttl = 60UL * 60UL;                       // hourly model run
  uint32_t now = (uint32_t)time(nullptr);
  CacheMeta m = _cache->stat("sounding");
  bool stale = force || !m.found || now < 1600000000UL || (now - m.fetchedAt) > ttl;
  if (!stale) return;

  // NOTE: rucsoundings.noaa.gov is DEAD as of 2026 (ECONNREFUSED on both 80/443,
  // confirmed off-device too) — NOAA decommissioned the host, so this fetch will
  // fail and the tab shows "sounding unavailable". Replacement planned: build a
  // forecast sounding from Open-Meteo pressure-level fields (temperature_*hPa,
  // dewpoint_*hPa, wind at each level) — same HTTPS API we already use for weather.
  // See BACKLOG. Plain HTTP kept for now (rucsoundings' TLS also failed on ESP32).
  // Parens URL-encoded (%28/%29).
  char url[220];
  snprintf(url, sizeof(url),
    "http://rucsoundings.noaa.gov/get_soundings.cgi?data_source=Op40&start=latest"
    "&n_hrs=1&fcst_len=shortest&airport=%.3f,%.3f&text=Ascii%%20text%%20%%28GSD%%20format%%29",
    _loc->active().lat, _loc->active().lon);
  _inflight = true;
  _net->get(url, [this](int code, const String& body) {
    _inflight = false;
    if (code == 200 && parse(body)) {
      _cache->put("sounding", body, code, (uint32_t)time(nullptr));
      _lastFetched = (uint32_t)time(nullptr);
      _status = ProviderStatus::Ready;
      Serial.printf("[sounding] %u levels, FZL=%.0fm\n", (unsigned)_levels.size(), _freezeM);
    } else {
      Serial.printf("[sounding] fetch failed code=%d len=%u\n", code, (unsigned)body.length());
      if (_levels.empty()) _status = ProviderStatus::Error;
    }
    if (_bus) _bus->publish(ProviderId::Weather);
  });
}

// GSD level lines: TYPE PRES HGT TEMP DEWPT WDIR WSPD  (pres tenths-mb, temp/dewpt
// tenths-C). Types 4=mandatory, 5=significant, 9=surface carry temp; 99999=missing.
bool SoundingProvider::parse(const String& gsd) {
  std::vector<SoundingLevel> out;
  int i = 0, n = gsd.length();
  while (i < n) {
    int eol = gsd.indexOf('\n', i); if (eol < 0) eol = n;
    String ln = gsd.substring(i, eol); i = eol + 1;
    ln.trim();
    if (!ln.length()) continue;
    char t = ln[0];
    if (t != '4' && t != '5' && t != '9') continue;   // temp-bearing levels
    // tokenise
    int tok[7]; int ntok = 0; int p = 0; int L = ln.length();
    while (p < L && ntok < 7) {
      while (p < L && ln[p] == ' ') ++p;
      int st = p; while (p < L && ln[p] != ' ') ++p;
      if (p > st) tok[ntok++] = atoi(ln.substring(st, p).c_str());
    }
    if (ntok < 5) continue;
    if (tok[0] != 4 && tok[0] != 5 && tok[0] != 9) continue;
    SoundingLevel lv;
    lv.altM  = tok[2];
    if (tok[3] == 99999) continue;
    lv.tempC = tok[3] / 10.0f;
    lv.dewpC = (tok[4] == 99999) ? -999 : tok[4] / 10.0f;
    if (ntok >= 7) { lv.wdir = tok[5]; lv.wspd = tok[6]; }
    out.push_back(lv);
  }
  if (out.size() < 3) return false;

  // Freezing level: first upward crossing of T = 0.
  float fz = -1;
  for (size_t k = 1; k < out.size(); ++k) {
    if (out[k - 1].tempC > 0 && out[k].tempC <= 0) {
      float f = out[k - 1].tempC / (out[k - 1].tempC - out[k].tempC);
      fz = out[k - 1].altM + f * (out[k].altM - out[k - 1].altM);
      break;
    }
  }
  _levels = std::move(out);
  _freezeM = fz;
  return true;
}
