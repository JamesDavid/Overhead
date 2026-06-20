#include "MetarStore.h"

void MetarStore::upsert(const MetarRec& r) {
  if (!r.icao.length()) return;
  for (auto& e : _recs)
    if (e.icao == r.icao) {                        // newer observation (or fresher fetch) replaces
      if (r.obsTime > e.obsTime || (r.obsTime == e.obsTime && r.fetchedAt >= e.fetchedAt)) {
        // keep any richer fields a sparser source (e.g. the pressure feed) lacks
        MetarRec m = r;
        if (m.tempC == -999 && e.tempC != -999)  m.tempC = e.tempC;
        if (m.dewpC == -999 && e.dewpC != -999)  m.dewpC = e.dewpC;
        if (m.cat.length() == 0)                 m.cat = e.cat;
        if (m.cloud < 0 && e.cloud >= 0)         m.cloud = e.cloud;
        if (m.visSm < 0 && e.visSm >= 0)         m.visSm = e.visSm;
        if (m.ceilingFt < 0 && e.ceilingFt >= 0) m.ceilingFt = e.ceilingFt;
        if (m.wspd < 0 && e.wspd >= 0)           { m.wspd = e.wspd; m.wdir = e.wdir; }
        e = m;
      }
      return;
    }
  if ((int)_recs.size() >= kMax) {                 // evict the stalest entry
    size_t old = 0; uint32_t t = 0xFFFFFFFFu;
    for (size_t i = 0; i < _recs.size(); ++i) if (_recs[i].fetchedAt < t) { t = _recs[i].fetchedAt; old = i; }
    _recs.erase(_recs.begin() + old);
  }
  _recs.push_back(r);
}

void MetarStore::inBox(double a0, double w0, double a1, double w1, std::vector<const MetarRec*>& out) const {
  for (auto& e : _recs)
    if (e.lat >= a0 && e.lat <= a1 && e.lon >= w0 && e.lon <= w1) out.push_back(&e);
}
