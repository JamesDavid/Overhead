#pragma once
#include <Arduino.h>
#include "TleProvider.h"   // ProviderStatus

class Settings;
class NetClient;
class Cache;
class EventBus;

// providers/SpaceWxProvider — NOAA SWPC space weather (spec §6, §9; no key).
// Planetary Kp + 10.7cm solar flux (SFI). Cached, refreshed every 15-30 min,
// publishes SpaceWx. Drives the HF band table and the Director's Kp trigger.
class SpaceWxProvider {
public:
  void begin(Settings* s, NetClient* net, Cache* cache, EventBus* bus);
  void refresh(bool force = false);

  float kp()  const { return _kp; }      // 0..9, -1 unknown
  static constexpr int kKpHist = 24;     // ~3 days of 3-hourly Kp
  const float* kpHist() const { return _kpHist; }
  int   kpHistN() const { return _kpHistN; }
  int   sfi() const { return _sfi; }     // solar flux units, -1 unknown
  const String& flareClass() const { return _flare; }  // GOES X-ray, e.g. "M1.2"; "" unknown
  int   windSpeed() const { return _windKms; }         // solar wind km/s, -1 unknown
  int   bz() const { return _bz; }                     // IMF Bz GSM (nT), -999 unknown
  ProviderStatus status() const { return _status; }
  uint32_t lastFetched() const { return _lastFetched; }

private:
  void fetchKp();
  void fetchSfi();
  void fetchXray();
  void fetchWind();
  void fetchMag();
  bool parseKp(const String& body);

  Settings*  _s = nullptr;
  NetClient* _net = nullptr;
  Cache*     _cache = nullptr;
  EventBus*  _bus = nullptr;

  float _kp = -1;
  float _kpHist[kKpHist] = {0};
  int   _kpHistN = 0;
  int   _sfi = -1;
  String _flare;
  int   _windKms = -1;
  int   _bz = -999;
  ProviderStatus _status = ProviderStatus::Loading;
  uint32_t _lastFetched = 0;
};
