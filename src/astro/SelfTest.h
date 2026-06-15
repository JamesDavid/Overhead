#pragma once

// astro/SelfTest — boot-time validation of the astro core (spec §10.2 "test vs
// a known pass"). Prints PASS/FAIL per module to serial. Gated by ASTRO_SELFTEST.
namespace astro {
bool runSelfTests();
}
