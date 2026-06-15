#include "SolarSystem.h"
#include "Time.h"
#include <math.h>

// Paul Schlyter, "Computing planetary positions" (public domain).
namespace astro {

static double rev(double x) { x = fmod(x, 360.0); return x < 0 ? x + 360 : x; }
static double sind(double d) { return sin(d * DEG2RAD); }
static double cosd(double d) { return cos(d * DEG2RAD); }
static double atan2d(double y, double x) { return atan2(y, x) * RAD2DEG; }

// Schlyter day number (epoch 1999-12-31 0:00 UT == JD 2451543.5).
static double schlyterDay(double jd) { return jd - 2451543.5; }

struct EclPos { double lon, lat, r; };  // geocentric ecliptic (deg, deg, AU/ER)

// Sun: returns ecliptic longitude, distance (AU) and rectangular xs,ys.
static void sun(double d, double& lonsun, double& rs, double& xs, double& ys) {
  double w = 282.9404 + 4.70935e-5 * d;
  double e = 0.016709 - 1.151e-9 * d;
  double M = rev(356.0470 + 0.9856002585 * d);
  double E = M + e * RAD2DEG * sind(M) * (1 + e * cosd(M));
  double xv = cosd(E) - e;
  double yv = sqrt(1 - e * e) * sind(E);
  double v = atan2d(yv, xv);
  rs = sqrt(xv * xv + yv * yv);
  lonsun = rev(v + w);
  xs = rs * cosd(lonsun);
  ys = rs * sind(lonsun);
}

static EclPos moon(double d) {
  double N = rev(125.1228 - 0.0529538083 * d);
  double i = 5.1454;
  double w = rev(318.0634 + 0.1643573223 * d);
  double a = 60.2666;            // Earth radii
  double e = 0.054900;
  double M = rev(115.3654 + 13.0649929509 * d);

  double E = M + e * RAD2DEG * sind(M) * (1 + e * cosd(M));
  for (int k = 0; k < 2; ++k)    // iterate (Moon's e is larger)
    E = E - (E - e * RAD2DEG * sind(E) - M) / (1 - e * cosd(E));

  double xv = a * (cosd(E) - e);
  double yv = a * sqrt(1 - e * e) * sind(E);
  double v = atan2d(yv, xv);
  double r = sqrt(xv * xv + yv * yv);

  double xh = r * (cosd(N) * cosd(v + w) - sind(N) * sind(v + w) * cosd(i));
  double yh = r * (sind(N) * cosd(v + w) + cosd(N) * sind(v + w) * cosd(i));
  double zh = r * (sind(v + w) * sind(i));

  double lon = rev(atan2d(yh, xh));
  double lat = atan2d(zh, sqrt(xh * xh + yh * yh));

  // Main perturbations (Schlyter): needs Sun + Moon mean elements.
  double Ms = rev(356.0470 + 0.9856002585 * d);
  double ws = 282.9404 + 4.70935e-5 * d;
  double Ls = rev(ws + Ms);
  double Lm = rev(N + w + M);
  double Mm = M;
  double D = rev(Lm - Ls);
  double F = rev(Lm - N);

  lon += -1.274 * sind(Mm - 2 * D) + 0.658 * sind(2 * D) - 0.186 * sind(Ms)
       - 0.059 * sind(2 * Mm - 2 * D) - 0.057 * sind(Mm - 2 * D + Ms)
       + 0.053 * sind(Mm + 2 * D) + 0.046 * sind(2 * D - Ms) + 0.041 * sind(Mm - Ms)
       - 0.035 * sind(D) - 0.031 * sind(Mm + Ms);
  lat += -0.173 * sind(F - 2 * D) - 0.055 * sind(Mm - F - 2 * D)
       - 0.046 * sind(Mm + F - 2 * D) + 0.033 * sind(F + 2 * D) + 0.017 * sind(2 * Mm + F);
  r += -0.58 * cosd(Mm - 2 * D) - 0.46 * cosd(2 * D);

  return { rev(lon), lat, r / 23454.8 };   // ER -> AU
}

// Heliocentric -> geocentric ecliptic for a planet (no major perturbations).
static EclPos planet(double N, double i, double w, double a, double e, double M,
                     double xs, double ys) {
  N = rev(N); w = rev(w); M = rev(M);
  double E = M + e * RAD2DEG * sind(M) * (1 + e * cosd(M));
  for (int k = 0; k < 3; ++k)
    E = E - (E - e * RAD2DEG * sind(E) - M) / (1 - e * cosd(E));
  double xv = a * (cosd(E) - e);
  double yv = a * sqrt(1 - e * e) * sind(E);
  double v = atan2d(yv, xv);
  double r = sqrt(xv * xv + yv * yv);
  double xh = r * (cosd(N) * cosd(v + w) - sind(N) * sind(v + w) * cosd(i));
  double yh = r * (sind(N) * cosd(v + w) + cosd(N) * sind(v + w) * cosd(i));
  double zh = r * (sind(v + w) * sind(i));
  double xg = xh + xs, yg = yh + ys, zg = zh;
  double lon = rev(atan2d(yg, xg));
  double lat = atan2d(zg, sqrt(xg * xg + yg * yg));
  return { lon, lat, sqrt(xg * xg + yg * yg + zg * zg) };
}

static EclPos geoEcliptic(Planet p, double d, double xs, double ys, double lonsun, double rs) {
  switch (p) {
    case Planet::Sun:   return { lonsun, 0, rs };
    case Planet::Moon:  return moon(d);
    case Planet::Mercury: return planet(48.3313 + 3.24587e-5 * d, 7.0047 + 5.00e-8 * d,
        29.1241 + 1.01444e-5 * d, 0.387098, 0.205635 + 5.59e-10 * d, 168.6562 + 4.0923344368 * d, xs, ys);
    case Planet::Venus:   return planet(76.6799 + 2.46590e-5 * d, 3.3946 + 2.75e-8 * d,
        54.8910 + 1.38374e-5 * d, 0.723330, 0.006773 - 1.302e-9 * d, 48.0052 + 1.6021302244 * d, xs, ys);
    case Planet::Mars:    return planet(49.5574 + 2.11081e-5 * d, 1.8497 - 1.78e-8 * d,
        286.5016 + 2.92961e-5 * d, 1.523688, 0.093405 + 2.516e-9 * d, 18.6021 + 0.5240207766 * d, xs, ys);
    case Planet::Jupiter: return planet(100.4542 + 2.76854e-5 * d, 1.3030 - 1.557e-7 * d,
        273.8777 + 1.64505e-5 * d, 5.20256, 0.048498 + 4.469e-9 * d, 19.8950 + 0.0830853001 * d, xs, ys);
    case Planet::Saturn:  return planet(113.6634 + 2.38980e-5 * d, 2.4886 - 1.081e-7 * d,
        339.3939 + 2.97661e-5 * d, 9.55475, 0.055546 - 9.499e-9 * d, 316.9670 + 0.0334442282 * d, xs, ys);
    case Planet::Uranus:  return planet(74.0005 + 1.3978e-5 * d, 0.7733 + 1.9e-8 * d,
        96.6612 + 3.0565e-5 * d, 19.18171 - 1.55e-8 * d, 0.047318 + 7.45e-9 * d, 142.5905 + 0.011725806 * d, xs, ys);
    case Planet::Neptune: return planet(131.7806 + 3.0173e-5 * d, 1.7700 - 2.55e-7 * d,
        272.8461 - 6.027e-6 * d, 30.05826 + 3.313e-8 * d, 0.008606 + 2.15e-9 * d, 260.2471 + 0.005995147 * d, xs, ys);
  }
  return { lonsun, 0, rs };
}

static void eclipticToRaDec(double lon, double lat, double d, double& raDeg, double& decDeg) {
  double oblecl = 23.4393 - 3.563e-7 * d;
  double xe = cosd(lon) * cosd(lat);
  double ye = sind(lon) * cosd(lat);
  double ze = sind(lat);
  double xq = xe;
  double yq = ye * cosd(oblecl) - ze * sind(oblecl);
  double zq = ye * sind(oblecl) + ze * cosd(oblecl);
  raDeg = rev(atan2d(yq, xq));
  decDeg = atan2d(zq, sqrt(xq * xq + yq * yq));
}

PlanetState planetState(Planet p, double jd, double latDeg, double lonDeg) {
  double d = schlyterDay(jd);
  double lonsun, rs, xs, ys;
  sun(d, lonsun, rs, xs, ys);
  EclPos e = geoEcliptic(p, d, xs, ys, lonsun, rs);

  PlanetState s;
  eclipticToRaDec(e.lon, e.lat, d, s.raDeg, s.decDeg);
  s.distanceAu = e.r;

  Equatorial eq{ s.raDeg * DEG2RAD, s.decDeg * DEG2RAD };
  Horizontal h = equatorialToHorizontal(eq, latDeg * DEG2RAD, lstRad(jd, lonDeg));
  s.azDeg = rev(h.azRad * RAD2DEG);
  s.elDeg = h.altRad * RAD2DEG;
  s.above = s.elDeg > 0;
  return s;
}

double moonPhaseDeg(double jd) {
  double d = schlyterDay(jd);
  double lonsun, rs, xs, ys;
  sun(d, lonsun, rs, xs, ys);
  EclPos m = moon(d);
  return rev(m.lon - lonsun);    // 0 new, 90 first qtr, 180 full, 270 last
}

double moonIlluminationPct(double jd) {
  double phase = moonPhaseDeg(jd) * DEG2RAD;
  return 50.0 * (1.0 - cos(phase));
}

const char* planetName(Planet p) {
  switch (p) {
    case Planet::Sun: return "Sun";       case Planet::Moon: return "Moon";
    case Planet::Mercury: return "Mercury"; case Planet::Venus: return "Venus";
    case Planet::Mars: return "Mars";     case Planet::Jupiter: return "Jupiter";
    case Planet::Saturn: return "Saturn"; case Planet::Uranus: return "Uranus";
    case Planet::Neptune: return "Neptune";
  }
  return "?";
}

} // namespace astro
