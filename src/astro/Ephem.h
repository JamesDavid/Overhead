#pragma once
#include "config.h"
#include <time.h>

// astro/Ephem — Sun/Moon/planet ephemeris (spec §5), wrapping MarScaper
// Ephemeris (VSOP87 + ELP2000). That library is GPLv3, so the ENTIRE
// implementation is confined behind ENABLE_EPHEM (default off, spec §11.7): a
// permissive build drops it and the methods report unavailable. The Director's
// day/night signal does NOT depend on this — it uses astro::Sun (permissive).
//
// When ENABLE_EPHEM is turned on (Solar System tab, m6), also add
// MarScaper/Ephemeris to lib_deps. The guarded code is written against that
// library's API and is validated then.
namespace astro {

enum class Body { Sun, Moon, Mercury, Venus, Mars, Jupiter, Saturn, Uranus, Neptune };

struct BodyState {
  bool   valid        = false;
  double azDeg        = 0;
  double elDeg        = 0;
  double raDeg        = 0;
  double decDeg       = 0;
  double distanceAu   = 0;
  double apparentDiamArcsec = 0;
  bool   aboveHorizon = false;
};

class Ephem {
public:
  static bool available();      // true only when ENABLE_EPHEM
  void   setObserver(double latDeg, double lonDeg, double altM);
  BodyState body(Body b, time_t utc);
  double moonIlluminationPct(time_t utc);   // -1 if unavailable

private:
  double _latDeg = 0, _lonDeg = 0, _altM = 0;
};

} // namespace astro
