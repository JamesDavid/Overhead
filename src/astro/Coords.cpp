#include "Coords.h"
#include "Time.h"
#include <math.h>

namespace astro {

Horizontal equatorialToHorizontal(const Equatorial& eq, double latRad, double lstRad) {
  double ha = lstRad - eq.raRad;                 // hour angle
  double sinAlt = sin(eq.decRad) * sin(latRad)
                + cos(eq.decRad) * cos(latRad) * cos(ha);
  double alt = asin(fmax(-1.0, fmin(1.0, sinAlt)));

  // Azimuth from North, eastward.
  double y = -cos(eq.decRad) * cos(latRad) * sin(ha);
  double x =  sin(eq.decRad) - sin(latRad) * sinAlt;
  double az = atan2(y, x);
  return { wrapTwoPi(az), alt };
}

double refractionDeg(double trueAltDeg) {
  if (trueAltDeg < -1.0) return 0.0;             // below horizon: ignore
  // Bennett (1982): R in arcminutes.
  double r = 1.0 / tan((trueAltDeg + 7.31 / (trueAltDeg + 4.4)) * DEG2RAD);
  return r / 60.0;                               // arcmin -> deg
}

} // namespace astro
