#pragma once
#include <Arduino.h>
#include <time.h>
#include <Sgp4.h>

// astro/SatEngine — SGP4 wrapper (spec §5). Wraps the Hopperpop SGP4 library
// (MIT) and exposes typed results: observer az/el, slant range, range-rate (for
// Doppler), the sub-satellite point, a sunlit/eclipse flag (cylindrical umbra
// test via astro::Sun), and pass prediction (AOS/TCA/LOS, max elevation).
namespace astro {

struct SatObservation {
  bool   valid        = false;
  double azDeg        = 0;
  double elDeg        = 0;
  double rangeKm      = 0;
  double rangeRateKmS = 0;   // + = receding (used for Doppler)
  double subLatDeg    = 0;
  double subLonDeg    = 0;
  double altKm        = 0;
  bool   sunlit       = false;
};

struct SatPass {
  bool     valid       = false;
  time_t   aos         = 0;
  time_t   tca         = 0;
  time_t   los         = 0;
  double   maxElDeg    = 0;
  uint32_t durationSec = 0;
};

class SatEngine {
public:
  void setObserver(double latDeg, double lonDeg, double altM);
  bool loadTle(const char* name, const char* line1, const char* line2);

  SatObservation observe(time_t utc);
  SatPass        nextPass(time_t fromUtc, double minElDeg = 0.0, int maxIter = 100);

  // Lightweight sub-satellite point (one propagation, no observer math) — for
  // sampling the ground track cheaply.
  struct SubPoint { double latDeg; double lonDeg; double altKm; };
  SubPoint subPoint(time_t utc);

  // Observed frequency for an emitted frequency, given range-rate (km/s):
  // f_obs = f * (1 - v_radial/c).
  static double dopplerHz(double emitHz, double rangeRateKmS);

  bool selfTest();   // known ISS TLE invariants -> serial

private:
  bool sunlit(double subLatDeg, double subLonDeg, double altKm, double jd) const;

  Sgp4   _sat;
  double _latDeg = 0, _lonDeg = 0, _altM = 0;
  bool   _haveTle = false;
  char   _name[28] = {0};
  char   _l1[74]   = {0};
  char   _l2[74]   = {0};
};

} // namespace astro
