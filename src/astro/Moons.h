#pragma once

// astro/Moons — telescopic "moons & rings" preview for the Solar System tab.
// Galilean moons of Jupiter via Meeus ch.44 low-accuracy (verified ~0.1 Rj vs JPL
// Horizons), and Saturn's ring-opening angle from the geocentric geometry.
namespace astro {

// Apparent E-W offsets of the 4 Galilean moons in Jupiter equatorial radii (sign =
// opposite sides of the disk). 0=Io, 1=Europa, 2=Ganymede, 3=Callisto.
void        galileanMoons(double jd, double x[4]);
const char* galileanName(int i);     // "Io" / "Europa" / "Ganymede" / "Callisto"
const char* galileanSym(int i);      // "Io"/"Eu"/"Ga"/"Ca"

// Saturn ring opening B (deg): ~0 = edge-on, |B| up to ~27 = wide open.
double saturnRingTiltDeg(double jd);

} // namespace astro
