#pragma once

// astro/Coords — coordinate transforms (spec §5). Equatorial (RA/Dec) <->
// horizontal (Alt/Az) for an observer, plus atmospheric refraction. Azimuth is
// measured from North, increasing eastward.
namespace astro {

struct Equatorial { double raRad;  double decRad; };
struct Horizontal { double azRad;  double altRad; };  // az from North, eastward

// observer latitude in radians; lst = local sidereal time in radians.
Horizontal equatorialToHorizontal(const Equatorial& eq, double latRad, double lstRad);

// Bennett atmospheric refraction for an apparent/true altitude in DEGREES;
// returns the correction to ADD to the true altitude, in degrees.
double refractionDeg(double trueAltDeg);

} // namespace astro
