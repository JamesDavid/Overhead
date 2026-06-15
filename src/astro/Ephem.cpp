#include "Ephem.h"

#if ENABLE_EPHEM
  #include <Ephemeris.h>   // MarScaper — GPLv3, compiled only when enabled

namespace astro {

static SolarSystemObjectIndex mapBody(Body b) {
  switch (b) {
    case Body::Sun:     return Sun;
    case Body::Moon:    return EarthsMoon;
    case Body::Mercury: return Mercury;
    case Body::Venus:   return Venus;
    case Body::Mars:    return Mars;
    case Body::Jupiter: return Jupiter;
    case Body::Saturn:  return Saturn;
    case Body::Uranus:  return Uranus;
    case Body::Neptune: return Neptune;
  }
  return Sun;
}

bool Ephem::available() { return true; }

void Ephem::setObserver(double latDeg, double lonDeg, double altM) {
  _latDeg = latDeg; _lonDeg = lonDeg; _altM = altM;
  Ephemeris::flipLongitude(false);
  Ephemeris::setLocationOnEarth((float)latDeg, (float)lonDeg);
  Ephemeris::setAltitude((int)altM);
}

BodyState Ephem::body(Body b, time_t utc) {
  BodyState s;
  struct tm tmv; gmtime_r(&utc, &tmv);
  SolarSystemObject o = Ephemeris::solarSystemObjectAtDateAndTime(
      mapBody(b), tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900,
      tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  s.azDeg        = o.horizontalCoordinates.azimuth;
  s.elDeg        = o.horizontalCoordinates.altitude;
  s.raDeg        = o.equaCoordinates.ra * 15.0;   // RA hours -> degrees
  s.decDeg       = o.equaCoordinates.dec;
  s.distanceAu   = o.distanceFromEarth;
  s.apparentDiamArcsec = o.angularDiameter;
  s.aboveHorizon = s.elDeg > 0;
  s.valid        = true;
  return s;
}

double Ephem::moonIlluminationPct(time_t utc) {
  // Illuminated fraction from Sun-Moon elongation (Sun ~ at infinity).
  BodyState sun  = body(Body::Sun,  utc);
  BodyState moon = body(Body::Moon, utc);
  if (!sun.valid || !moon.valid) return -1;
  double d2r = 3.14159265358979323846 / 180.0;
  double cosElong = sin(sun.decDeg * d2r) * sin(moon.decDeg * d2r)
                  + cos(sun.decDeg * d2r) * cos(moon.decDeg * d2r)
                  * cos((sun.raDeg - moon.raDeg) * d2r);
  double elong = acos(fmax(-1.0, fmin(1.0, cosElong)));
  return 50.0 * (1.0 - cos(elong));   // (1 - cos(phase))/2 * 100, phase~elong
}

} // namespace astro

#else  // ----- ENABLE_EPHEM off: permissive build, no GPLv3 dependency --------

namespace astro {
bool      Ephem::available() { return false; }
void      Ephem::setObserver(double, double, double) {}
BodyState Ephem::body(Body, time_t) { return BodyState{}; }
double    Ephem::moonIlluminationPct(time_t) { return -1; }
}

#endif
