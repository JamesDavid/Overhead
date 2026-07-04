// Overhead Companion Rotor — scaffold (milestone 1).
// The baseline pointer firmware (spec §11) lands here in milestone 2. For now this is a
// minimal translation unit that proves the build works and the shared contract is reachable.
#include <Arduino.h>
#include "shared/telemetry.h"   // via -I <monorepo root> (rotor/platformio.ini)

// The wire format is fixed-size + packed; guard it so a silent struct-layout drift
// (the classic split-repo failure) fails the BUILD, not a field in the field.
static_assert(sizeof(telemetry_t) == 36, "telemetry_t layout drifted — check shared/telemetry.h");

void setup() {
  Serial.begin(115200);
  Serial.printf("[rotor] scaffold up. telemetry_t=%u bytes, proto=%d\n",
                (unsigned)sizeof(telemetry_t), TELEM_PROTO_VER);
}

void loop() {}
