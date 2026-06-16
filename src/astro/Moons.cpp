#include "Moons.h"
#include "SolarSystem.h"
#include <math.h>

namespace astro {

static double rd(double d) { return d * 3.14159265358979323846 / 180.0; }

// Meeus, Astronomical Algorithms ch.44 (low accuracy). Returns each moon's apparent
// E-W offset in Jupiter equatorial radii.
void galileanMoons(double jd, double x[4]) {
  double d = jd - 2451545.0;
  double V = 172.74 + 0.00111588 * d;
  double M = rd(357.529 + 0.9856003 * d);
  double N = 20.020 + 0.0830853 * d + 0.329 * sin(rd(V));
  double J = 66.115 + 0.9025179 * d - 0.329 * sin(rd(V));
  double A = 1.915 * sin(M) + 0.020 * sin(2 * M);
  double B = 5.555 * sin(rd(N)) + 0.168 * sin(rd(2 * N));
  double K = rd(J + A - B);
  double Re = 1.00014 - 0.01671 * cos(M) - 0.00014 * cos(2 * M);         // Earth-Sun (AU)
  double RJ = 5.20872 - 0.25208 * cos(rd(N)) - 0.00611 * cos(rd(2 * N)); // Jupiter-Sun (AU)
  double De = sqrt(Re * Re + RJ * RJ - 2 * Re * RJ * cos(K));            // Earth-Jupiter (AU)
  double psi = asin(Re / De * sin(K));
  double dl = d - De / 173.0;                                           // light-time
  double pb = psi - rd(B);
  double u[4] = {
    rd(163.8067 + 203.4058643 * dl) + pb,
    rd(358.4108 + 101.2916334 * dl) + pb,
    rd(5.7129   + 50.2345179  * dl) + pb,
    rd(224.8151 + 21.4879801  * dl) + pb,
  };
  double G = rd(331.18 + 50.310482 * dl), H = rd(87.40 + 21.569231 * dl);
  double r[4] = {
    5.9073  - 0.0244 * cos(2 * (u[0] - u[1])),
    9.3991  - 0.0882 * cos(2 * (u[1] - u[2])),
    14.9924 - 0.0216 * cos(G),
    26.3699 - 0.1935 * cos(H),
  };
  for (int i = 0; i < 4; ++i) x[i] = r[i] * sin(u[i]);
}

const char* galileanName(int i) {
  static const char* n[4] = {"Io", "Europa", "Ganymede", "Callisto"};
  return (i >= 0 && i < 4) ? n[i] : "?";
}
const char* galileanSym(int i) {
  static const char* s[4] = {"Io", "Eu", "Ga", "Ca"};
  return (i >= 0 && i < 4) ? s[i] : "?";
}

// Ring opening: from Saturn's geocentric ecliptic longitude vs the ring-plane node.
// (Saturn's ~2.5 deg ecliptic latitude is neglected — gives B within a couple deg.)
double saturnRingTiltDeg(double jd) {
  HelioPos s = heliocentricBody(5, jd);   // Saturn
  HelioPos e = heliocentricBody(2, jd);   // Earth
  double sx = s.rAu * cos(rd(s.lonDeg)), sy = s.rAu * sin(rd(s.lonDeg));
  double ex = e.rAu * cos(rd(e.lonDeg)), ey = e.rAu * sin(rd(e.lonDeg));
  double lambda = atan2(sy - ey, sx - ex);            // geocentric ecliptic lon (rad)
  const double inc = rd(28.075), node = rd(169.508);  // ring plane
  double B = asin(sin(inc) * sin(lambda - node));
  return B / (3.14159265358979323846 / 180.0);
}

} // namespace astro
