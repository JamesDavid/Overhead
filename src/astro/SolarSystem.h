#pragma once
#include "Coords.h"

// astro/SolarSystem — permissive low-precision Sun/Moon/planet positions
// (Paul Schlyter's method; ~1-2 arcmin, Moon ~few arcmin with the main
// perturbations). Public-domain math, so the Solar System tab works on a
// permissive build with NO GPLv3 Ephem dependency. The GPLv3 astro/Ephem
// (VSOP87/ELP2000) remains available behind ENABLE_EPHEM for high precision.
namespace astro {

enum class Planet { Sun, Moon, Mercury, Venus, Mars, Jupiter, Saturn, Uranus, Neptune };

struct PlanetState {
  double raDeg = 0, decDeg = 0;
  double azDeg = 0, elDeg = 0;
  double distanceAu = 0;     // Sun/planets in AU; Moon ~0.0026 AU (x149.6e6 = km)
  bool   above = false;      // above the horizon
};

PlanetState planetState(Planet p, double jd, double latDeg, double lonDeg);

double moonIlluminationPct(double jd);   // 0..100
double moonPhaseDeg(double jd);          // 0 new, 90 first qtr, 180 full, 270 last
const char* planetName(Planet p);

} // namespace astro
