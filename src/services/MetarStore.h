#pragma once
#include <Arduino.h>
#include <vector>
#include <time.h>

// services/MetarStore — a shared per-airport METAR pool. Every aviation consumer
// fetches from the same aviationweather.gov METAR API but for different boxes
// (the nearby list, the pressure-map spread, a drill-in region). Instead of three
// disconnected caches, each feed UPSERTS the stations it parses into this one
// per-ICAO store, and consumers can read the union for a box. So a station fetched
// by one feed is visible to the others (no more "AWC unavailable on the list but
// the pressure map has data"), and they stay mutually consistent.
struct MetarRec {
  String   icao;
  float    lat = 0, lon = 0;
  int      hpa = -1, cloud = -1, wdir = -1, wspd = -1, tempC = -999, dewpC = -999;
  float    visSm = -1;        // visibility (statute miles); -1 unknown
  int      ceilingFt = -1;    // lowest BKN/OVC base (ft); -1 = none/unknown
  String   cat;               // VFR|MVFR|IFR|LIFR
  time_t   obsTime = 0;       // observation time; newer obs wins on upsert
  uint32_t fetchedAt = 0;
  // NOTE: raw METAR text is deliberately NOT pooled here — 80 records * ~80 chars of
  // String would push the no-PSRAM TLS heap floor. Raw stays on AviationWxProvider's
  // nearby list; the cache holds the decoded fields every consumer needs.
};

class MetarStore {
public:
  static MetarStore& instance() { static MetarStore s; return s; }

  void upsert(const MetarRec& r);                 // merge by ICAO (newest wins), bounded + LRU-evicted
  void inBox(double a0, double w0, double a1, double w1, std::vector<const MetarRec*>& out) const;
  const std::vector<MetarRec>& all() const { return _recs; }

private:
  std::vector<MetarRec> _recs;
  static constexpr int kMax = 80;                 // bound RAM on the no-PSRAM board
};
