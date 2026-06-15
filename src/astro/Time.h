#pragma once
#include <time.h>

// astro/Time — the shared time/sidereal math (spec §5). Written once, used by
// Coords, SatEngine, Ephem and the Director. All angles in radians unless a
// name says Deg.
namespace astro {

// Named kPI / kTwoPi (not PI/TWO_PI) — Arduino.h defines those as macros.
constexpr double kPI     = 3.14159265358979323846;
constexpr double kTwoPi  = 2.0 * kPI;
constexpr double DEG2RAD = kPI / 180.0;
constexpr double RAD2DEG = 180.0 / kPI;

double julianDate(time_t utc);        // Julian Date (days) from a unix UTC time
double julianCenturies(double jd);    // centuries from J2000.0
double gmstRad(double jd);            // Greenwich mean sidereal time [0,2pi)
double lstRad(double jd, double lonDeg); // local mean sidereal time [0,2pi)
double deltaTSeconds(int year);       // approx TT - UT1 (Espenak/Meeus, 2005-2050)

double wrapTwoPi(double a);           // normalise to [0,2pi)

} // namespace astro
