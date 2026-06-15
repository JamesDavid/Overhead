#include "Time.h"
#include <math.h>

namespace astro {

double wrapTwoPi(double a) {
  a = fmod(a, kTwoPi);
  return a < 0 ? a + kTwoPi : a;
}

double julianDate(time_t utc) {
  return (double)utc / 86400.0 + 2440587.5;
}

double julianCenturies(double jd) {
  return (jd - 2451545.0) / 36525.0;
}

// IAU 1982 GMST.
double gmstRad(double jd) {
  double d = jd - 2451545.0;
  double T = d / 36525.0;
  double gmstDeg = 280.46061837 + 360.98564736629 * d
                 + 0.000387933 * T * T - (T * T * T) / 38710000.0;
  gmstDeg = fmod(gmstDeg, 360.0);
  if (gmstDeg < 0) gmstDeg += 360.0;
  return gmstDeg * DEG2RAD;
}

double lstRad(double jd, double lonDeg) {
  return wrapTwoPi(gmstRad(jd) + lonDeg * DEG2RAD);
}

// Polynomial valid ~2005-2050 (Espenak & Meeus); ~seconds accuracy — fine for
// pass timing / sidereal use here.
double deltaTSeconds(int year) {
  double t = year - 2000.0;
  return 62.92 + 0.32217 * t + 0.005589 * t * t;
}

} // namespace astro
