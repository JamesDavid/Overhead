#include "SelfTest.h"
#include "Time.h"
#include "Coords.h"
#include "Sun.h"
#include "SatEngine.h"
#include <Arduino.h>
#include <math.h>

namespace astro {

static bool approx(double a, double b, double tol) { return fabs(a - b) <= tol; }

bool runSelfTests() {
  bool ok = true;
  Serial.println("\n[selftest] ===== astro core =====");

  // Time: GMST at J2000.0 (JD 2451545.0) is ~280.46 deg.
  double gmstDeg = gmstRad(2451545.0) * RAD2DEG;
  Serial.printf("[selftest] GMST(J2000)=%.3f deg (expect ~280.46)\n", gmstDeg);
  if (!approx(gmstDeg, 280.46, 0.1)) { Serial.println("  FAIL Time/GMST"); ok = false; }

  // Sun: at the March 2024 equinox the Sun's declination is ~0.
  // 2024-03-20 03:06 UTC ~ unix 1710904000.
  Equatorial s = sunRaDec(julianDate((time_t)1710904000));
  Serial.printf("[selftest] Sun dec at equinox=%.2f deg (expect ~0)\n", s.decRad * RAD2DEG);
  if (!approx(s.decRad * RAD2DEG, 0.0, 1.0)) { Serial.println("  FAIL Sun/equinox"); ok = false; }

  // Coords: an object on the meridian (HA=0) at dec=lat sits at the zenith.
  double lat = 40.0 * DEG2RAD;
  Equatorial atZenith{ /*ra*/ 1.234, /*dec*/ lat };
  Horizontal h = equatorialToHorizontal(atZenith, lat, /*lst==ra*/ 1.234);
  Serial.printf("[selftest] zenith alt=%.2f deg (expect ~90)\n", h.altRad * RAD2DEG);
  if (!approx(h.altRad * RAD2DEG, 90.0, 0.5)) { Serial.println("  FAIL Coords/zenith"); ok = false; }

  // SatEngine (SGP4) invariants against a known ISS TLE.
  SatEngine iss;
  if (!iss.selfTest()) ok = false;

  Serial.printf("[selftest] ===== astro core %s =====\n\n", ok ? "PASS" : "FAIL");
  return ok;
}

} // namespace astro
