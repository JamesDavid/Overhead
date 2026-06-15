#include "SatEngine.h"
#include "Time.h"
#include "Sun.h"
#include <math.h>
#include <string.h>

namespace astro {

static constexpr double kEarthRadiusKm = 6378.137;
static constexpr double kCmS           = 299792458.0;

static time_t jdToTime(double jd) {
  return (time_t)llround((jd - 2440587.5) * 86400.0);
}

void SatEngine::setObserver(double latDeg, double lonDeg, double altM) {
  _latDeg = latDeg; _lonDeg = lonDeg; _altM = altM;
  _sat.site(latDeg, lonDeg, altM);
}

bool SatEngine::loadTle(const char* name, const char* line1, const char* line2) {
  strncpy(_name, name,  sizeof(_name) - 1);
  strncpy(_l1,   line1, sizeof(_l1)   - 1);
  strncpy(_l2,   line2, sizeof(_l2)   - 1);
  _sat.init(_name, _l1, _l2);
  _haveTle = true;
  return true;
}

SatObservation SatEngine::observe(time_t utc) {
  SatObservation o;
  if (!_haveTle) return o;

  _sat.findsat((unsigned long)utc);
  o.azDeg     = _sat.satAz;
  o.elDeg     = _sat.satEl;
  o.rangeKm   = _sat.satDist;
  o.subLatDeg = _sat.satLat;
  o.subLonDeg = _sat.satLon;
  o.altKm     = _sat.satAlt;

  // Range-rate by 1 s finite difference (for Doppler).
  double d0 = _sat.satDist;
  _sat.findsat((unsigned long)utc + 1);
  o.rangeRateKmS = _sat.satDist - d0;

  o.sunlit = sunlit(o.subLatDeg, o.subLonDeg, o.altKm, julianDate(utc));
  o.valid  = true;
  return o;
}

SatPass SatEngine::nextPass(time_t fromUtc, double minElDeg, int maxIter) {
  SatPass p;
  if (!_haveTle) return p;

  _sat.initpredpoint((unsigned long)fromUtc, minElDeg);  // unix overload (NOT the JD one)
  passinfo op;
  if (!_sat.nextpass(&op, maxIter)) return p;

  p.aos      = jdToTime(op.jdstart);
  p.los      = jdToTime(op.jdstop);
  p.tca      = jdToTime(op.jdmax);
  p.maxElDeg = op.maxelevation;
  p.durationSec = (p.los > p.aos) ? (uint32_t)(p.los - p.aos) : 0;
  p.valid    = true;
  return p;
}

SatEngine::SubPoint SatEngine::subPoint(time_t utc) {
  SubPoint s{0, 0, 0};
  if (!_haveTle) return s;
  _sat.findsat((unsigned long)utc);
  s.latDeg = _sat.satLat;
  s.lonDeg = _sat.satLon;
  s.altKm  = _sat.satAlt;
  return s;
}

double SatEngine::dopplerHz(double emitHz, double rangeRateKmS) {
  return emitHz * (1.0 - (rangeRateKmS * 1000.0) / kCmS);
}

// Cylindrical-umbra eclipse test: reconstruct the satellite ECI position from
// its sub-point + altitude (spherical Earth is plenty here) and compare against
// the Sun direction. Sunlit unless on the anti-sun side AND within the shadow
// cylinder.
bool SatEngine::sunlit(double subLatDeg, double subLonDeg, double altKm, double jd) const {
  double gmst = gmstRad(jd);
  double r = kEarthRadiusKm + altKm;
  double latR = subLatDeg * DEG2RAD, lonR = subLonDeg * DEG2RAD;

  double xe = r * cos(latR) * cos(lonR);
  double ye = r * cos(latR) * sin(lonR);
  double ze = r * sin(latR);

  // ECEF -> ECI (rotate +GMST about z).
  double xi = xe * cos(gmst) - ye * sin(gmst);
  double yi = xe * sin(gmst) + ye * cos(gmst);
  double zi = ze;

  double s[3]; sunEciUnit(jd, s);
  double dotp = xi * s[0] + yi * s[1] + zi * s[2];
  if (dotp > 0) return true;                       // sun-facing hemisphere

  double px = xi - dotp * s[0];
  double py = yi - dotp * s[1];
  double pz = zi - dotp * s[2];
  double perp = sqrt(px * px + py * py + pz * pz);
  return perp > kEarthRadiusKm;                    // outside the umbra cylinder
}

bool SatEngine::selfTest() {
  // A real (epoch-2024) ISS TLE. Exact accuracy isn't the point — the SGP4
  // invariants (LEO altitude, |lat| <= inclination, a findable pass) hold for
  // years and validate the whole pipeline. Refresh from Celestrak in m3.
  const char* name = "ISS (ZARYA)";
  const char* l1 = "1 25544U 98067A   24010.51782528  .00016717  00000-0  30074-3 0  9993";
  const char* l2 = "2 25544  51.6416 247.4627 0006703 130.5360 325.0288 15.49815174 18769";

  setObserver(33.4255, -111.9400, 360.0);          // Tempe, AZ
  loadTle(name, l1, l2);

  // Pick a time near the TLE epoch (2024-01-10 12:25 UTC ~ 1704889528).
  time_t t = 1704889528;
  SatObservation o = observe(t);

  bool ok = true;
  Serial.println("[selftest] --- SatEngine (ISS) ---");
  Serial.printf("  subpoint %.2f,%.2f  alt=%.1fkm  az/el=%.1f/%.1f  rr=%.3fkm/s  %s\n",
                o.subLatDeg, o.subLonDeg, o.altKm, o.azDeg, o.elDeg,
                o.rangeRateKmS, o.sunlit ? "sunlit" : "eclipsed");

  if (!o.valid)                          { Serial.println("  FAIL: invalid obs"); ok = false; }
  if (o.altKm < 350 || o.altKm > 460)    { Serial.println("  FAIL: altitude out of LEO band"); ok = false; }
  if (fabs(o.subLatDeg) > 53.0)          { Serial.println("  FAIL: |lat| > inclination"); ok = false; }

  SatPass p = nextPass(t, 10.0, 50);
  if (p.valid) {
    Serial.printf("  next pass: maxEl=%.1f  dur=%us  (aos %ld -> los %ld)\n",
                  p.maxElDeg, p.durationSec, (long)p.aos, (long)p.los);
    if (p.maxElDeg < 10.0 || p.durationSec == 0 || p.durationSec > 1200) {
      Serial.println("  FAIL: implausible pass"); ok = false;
    }
  } else {
    Serial.println("  FAIL: no pass found"); ok = false;
  }

  // Doppler sanity: 145.8 MHz at -5 km/s closing should shift up a few kHz.
  double f = dopplerHz(145.800e6, -5.0);
  Serial.printf("  doppler 145.800MHz @ -5km/s -> %.4fMHz\n", f / 1e6);

  Serial.printf("[selftest] SatEngine %s\n", ok ? "PASS" : "FAIL");
  return ok;
}

} // namespace astro
