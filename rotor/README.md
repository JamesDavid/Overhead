# Overhead Companion Rotor

A small **alt/az pointer** that physically slews to whatever the Overhead dashboard is tracking
(ISS, a satellite, a planet). It's a *dumb pointer*: Overhead broadcasts az/el over ESP‑NOW, the
rotor homes itself and tracks. Full design: [`Overhead_Rotor_Spec.md`](Overhead_Rotor_Spec.md).

The wire format is shared with the dashboard (and the future radio node) in one place:
[`../shared/telemetry.h`](../shared/telemetry.h) — both sides `#include` it so it can't drift.

## Build

Standalone PlatformIO project (Arduino‑ESP32 **core 3.x**, target plain ESP32 WROOM). It reuses the
CrowPanel env's isolated core dir to avoid a package clash with the dashboard build. **Two build
targets** pick the motor/driver preset in [`src/config.h`](src/config.h):

```powershell
$env:PLATFORMIO_CORE_DIR = "$env:USERPROFILE\.platformio-crowpanel"
pio run -d rotor -e byj              # 28BYJ-48 + ULN2003 (default)
pio run -d rotor -e nema             # NEMA 17 + A4988/TMC2209 (STEP/DIR)
pio run -d rotor -e byj -t upload    # build + flash over USB
```

You do **not** enter a gear ratio — calibration measures it. If your pins/wiring differ from a
preset, edit its block in `config.h` and rebuild that env.

**Switches (fit what your build has):**

- **Az home switch** — **required.** Mechanical az zero + the reference for az auto‑cal (`AZ_LIMIT_PIN`, GPIO 34).
- **El home switch** — optional. Set `EL_LIMIT_PIN` to home el to a horizon switch; leave it `-1` to home el off the accelerometer (gravity). Either way the accelerometer trims el while tracking.
- **Travel end‑stops** — optional safety limits. Set `AZ_CW_LIMIT_PIN`/`AZ_CCW_LIMIT_PIN` and/or `EL_MIN_LIMIT_PIN`/`EL_MAX_LIMIT_PIN`; the axis won't drive past an active stop (guards cable‑wrap / over‑travel). All default `-1` (off).

Every switch wires to **GND** and reads active‑LOW via `INPUT_PULLUP` (GPIO34‑39 have no internal pull‑up — add an external ~10k to 3V3).

**Pinout** (edit the preset in `config.h` if yours differs):

| Signal | 28BYJ‑48 (ULN2003) | NEMA 17 (A4988/TMC2209) |
|---|---|---|
| Az motor | IN1–4 → 32, 33, 25, 26 | STEP 26, DIR 25, EN 33 |
| El motor | IN1–4 → 27, 14, 12, 13 | STEP 17, DIR 16, EN 4 |
| Az home switch | GPIO 34 ↔ GND | GPIO 34 ↔ GND |
| IMU (MPU6050, **6‑DOF**) | SDA 21, SCL 22, `0x68` | SDA 21, SCL 22, `0x68` |

The IMU is a **6‑DOF MPU6050** — only the accelerometer is used (el reference + trim). A 9‑DOF's
magnetometer isn't read (az heading comes from the home switch + step count + `SETNORTH`) and would
be swamped by the steppers anyway.

## Flash

**Web flasher (easiest):** <https://jamesdavid.github.io/Overhead/rotor-flasher/> — open in
**Chrome / Edge**, pick the build (**28BYJ** or **NEMA 17**) from the dropdown, plug the ESP32 in over
USB, click *Install*, and choose the serial port. (Source lives in
[`../docs/rotor-flasher/`](../docs/rotor-flasher/), served by the same GitHub Pages as the dashboard
flasher.)

Custom pins/ratios: edit `config.h` and flash from source
(`pio run -d rotor -e byj|nema -t upload`).

## Calibrate — no gear‑ratio math

Open a serial monitor at **115200** and use the menu. Results persist to NVS and survive reflash.

| Command | What it does |
|---|---|
| `CAL EL` | Measures el **steps/deg** against gravity (the accelerometer) — automatic, exact. |
| `CAL AZ` | Homes to the limit switch, jogs one full turn back to it → az **steps/deg** = steps/360. Falls back to a guided `MARK` cal if cable‑wrap blocks a full turn. |
| `SETNORTH` | Jog the pointer to true north first, then this stores the north offset. |
| `AZ± <deg>` / `EL± <deg>` | Jog an axis (used for `SETNORTH` and the manual az fallback). |
| `MARK` | Manual az cal: send at a reference mark, jog 360° back, send again. |
| `SHOW` | Print current calibration. |
| `HOME` | Re‑home. |
| `RESET` | Clear NVS back to `config.h` defaults. |

Typical first run: `CAL EL` → `CAL AZ` → jog to north → `SETNORTH`. Done.

## Deferred (not in this firmware)

The Overhead **sender** side (broadcasting `telemetry_t`), the repo relocation, and the **radio /
Doppler** node are deferred — see §10 of the spec. `// DEFERRED:` markers sit at those seams.
