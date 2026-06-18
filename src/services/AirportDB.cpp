#include "AirportDB.h"
#include <LittleFS.h>
#include <math.h>

// Mirrors TYPE_LABELS in tools/gen_airports.py (code -> on-screen label).
static const char* const kLabel[16] = {
  "TWR", "GND", "ATIS", "CLR", "CTAF", "UNI", "APP", "DEP",
  "A/D", "AWOS", "RDO", "CTR", "AFIS", "FSS", "ATF", "MISC"};
static constexpr double D2R = 3.14159265358979323846 / 180.0;
static constexpr int REC = 11;          // packed airport record: 4 + 2 + 2 + 2 + 1

const char* AirportDB::label(uint8_t c) { return kLabel[c < 16 ? c : 15]; }

static int16_t  rd16(const uint8_t* p) { return (int16_t)(p[0] | (p[1] << 8)); }
static uint16_t rdu16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(File& f) { uint8_t b[4]; f.read(b, 4); return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }

bool AirportDB::begin(const char* path) {
  _path = path; _ok = false; _lat = 999;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  uint8_t magic[4];
  if (f.read(magic, 4) != 4 || memcmp(magic, "AP01", 4) != 0) { f.close(); return false; }
  _nA = rd32(f); _nF = rd32(f);
  f.close();
  _recBase  = 12;
  _typeBase = _recBase + (uint32_t)_nA * REC;
  _f40Base  = _typeBase + _nF;
  _ok = (_nA > 0 && _nA < 100000);
  return _ok;
}

const AirportDB::Nearest& AirportDB::nearest(double latDeg, double lonDeg) {
  if (!_ok) { _near.valid = false; return _near; }
  if (_near.valid && fabs(latDeg - _lat) < 0.01 && fabs(lonDeg - _lon) < 0.01) return _near;
  _lat = latDeg; _lon = lonDeg; _near.valid = false;

  File f = LittleFS.open(_path, "r");
  if (!f) return _near;
  f.seek(_recBase);
  const int CH = 64; uint8_t buf[CH * REC];
  double clat = cos(latDeg * D2R), bestd2 = 1e18;
  int best = -1; int16_t bLat = 0, bLon = 0; uint16_t bOff = 0; uint8_t bCnt = 0, bId[4] = {0};
  for (uint32_t i0 = 0; i0 < _nA; i0 += CH) {
    int m = (int)min((uint32_t)CH, _nA - i0);
    if (f.read(buf, m * REC) != m * REC) break;
    for (int j = 0; j < m; ++j) {
      const uint8_t* r = buf + j * REC;
      int16_t la = rd16(r + 4), lo = rd16(r + 6);
      double dla = la / 100.0 - latDeg, dlo = (lo / 100.0 - lonDeg) * clat;
      double d2 = dla * dla + dlo * dlo;
      if (d2 < bestd2) { bestd2 = d2; best = (int)(i0 + j); bLat = la; bLon = lo; bOff = rdu16(r + 8); bCnt = r[10]; memcpy(bId, r, 4); }
    }
  }
  if (best < 0) { f.close(); return _near; }

  double dla = bLat / 100.0 - latDeg, dlo = (bLon / 100.0 - lonDeg) * clat;
  _near.distNm = (float)(sqrt(dla * dla + dlo * dlo) * 60.0);
  _near.brgDeg = (float)(atan2(dlo, dla) / D2R); if (_near.brgDeg < 0) _near.brgDeg += 360;
  memcpy(_near.id, bId, 4); _near.id[4] = 0;
  for (int k = 3; k >= 0 && _near.id[k] == ' '; --k) _near.id[k] = 0;

  int n = bCnt < kMaxFreq ? bCnt : kMaxFreq;
  uint8_t tb[kMaxFreq]; uint8_t vb[kMaxFreq * 2];
  f.seek(_typeBase + bOff);          f.read(tb, n);
  f.seek(_f40Base + (uint32_t)bOff * 2); f.read(vb, n * 2);
  for (int k = 0; k < n; ++k) { _near.type[k] = tb[k]; _near.f40[k] = rdu16(vb + k * 2); }
  _near.n = (uint8_t)n;
  _near.valid = true;
  f.close();
  return _near;
}
