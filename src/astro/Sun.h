#pragma once
#include "Coords.h"

// astro/Sun — permissive, low-precision solar position (~0.01 deg; Meeus
// "low accuracy"). Public-domain math, deliberately NOT the GPLv3 Ephem lib, so
// it can drive the satellite sunlit/eclipse test and (later) the Director's
// day/night ambient + theme without any license entanglement.
namespace astro {

Equatorial sunRaDec(double jd);                 // apparent RA/Dec of the Sun

// Unit vector to the Sun in an Earth-centred inertial (equatorial) frame —
// good enough for the cylindrical-umbra eclipse test.
void sunEciUnit(double jd, double out[3]);

// Sun altitude in degrees for an observer (drives day/night). Negative = below
// the horizon; < -6 civil, < -12 nautical, < -18 astronomical twilight.
double sunAltitudeDeg(double jd, double latDeg, double lonDeg);

} // namespace astro
