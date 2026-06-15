#include "Sun.h"
#include "Time.h"
#include <math.h>

namespace astro {

Equatorial sunRaDec(double jd) {
  double n = jd - 2451545.0;
  double L = fmod(280.460 + 0.9856474 * n, 360.0);          // mean longitude
  double g = fmod(357.528 + 0.9856003 * n, 360.0) * DEG2RAD; // mean anomaly
  double lambda = (L + 1.915 * sin(g) + 0.020 * sin(2 * g)) * DEG2RAD; // ecliptic lon
  double eps = (23.439 - 0.0000004 * n) * DEG2RAD;            // obliquity

  Equatorial eq;
  eq.raRad  = atan2(cos(eps) * sin(lambda), cos(lambda));
  eq.decRad = asin(sin(eps) * sin(lambda));
  if (eq.raRad < 0) eq.raRad += kTwoPi;
  return eq;
}

void sunEciUnit(double jd, double out[3]) {
  Equatorial s = sunRaDec(jd);
  out[0] = cos(s.decRad) * cos(s.raRad);
  out[1] = cos(s.decRad) * sin(s.raRad);
  out[2] = sin(s.decRad);
}

double sunAltitudeDeg(double jd, double latDeg, double lonDeg) {
  Equatorial s = sunRaDec(jd);
  double lst = lstRad(jd, lonDeg);
  Horizontal h = equatorialToHorizontal(s, latDeg * DEG2RAD, lst);
  return h.altRad * RAD2DEG;
}

} // namespace astro
