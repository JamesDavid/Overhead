# Overhead Companion Rotor

A small **alt/az pointer** that physically slews to whatever the Overhead dashboard is tracking
(ISS, a satellite, a planet). It's a *dumb pointer*: Overhead broadcasts az/el over ESP‑NOW, the
rotor homes itself and tracks. Full design: [`Overhead_Rotor_Spec.md`](Overhead_Rotor_Spec.md).

The wire format is shared with the dashboard (and the future radio node) in one place:
[`../shared/telemetry.h`](../shared/telemetry.h) — both sides `#include` it so it can't drift.

## Build

Standalone PlatformIO project (Arduino‑ESP32 **core 3.x**, target plain ESP32 WROOM). It reuses the
CrowPanel env's isolated core dir. **One firmware** — motor type, pins, switches, and channel are set
at runtime in the browser (see [Configure](#configure-in-the-browser)), so there's a single build:

```powershell
$env:PLATFORMIO_CORE_DIR = "$env:USERPROFILE\.platformio-crowpanel"
# use the -crowpanel penv's OWN launcher (Python 3.11); the registry one is 3.12 and breaks the build
$pio = "$env:USERPROFILE\.platformio-crowpanel\penv\Scripts\platformio.exe"
& $pio run -d rotor -e rotor              # build
& $pio run -d rotor -e rotor -t upload    # build + flash over USB
```

You do **not** enter a gear ratio — calibration measures it. `config.h` holds only the factory
**defaults** (28BYJ pinout); every field is overridable at runtime from the web UI.

**Switches** — all assigned in the web UI; the defaults below ship off except az home:

- **Az home switch** — **required.** Az zero + the reference for az auto‑cal (default GPIO 34).
- **El home switch** — optional. Assign a GPIO to home el to a horizon switch; leave it unset to home el off the accelerometer (gravity). Either way the accelerometer trims el while tracking.
- **Travel end‑stops** — optional safety limits (az CW/CCW, el min/max); the axis won't drive past an active stop (guards cable‑wrap / over‑travel).

Every switch wires to **GND** and reads active‑LOW via `INPUT_PULLUP` (GPIO34‑39 have no internal pull‑up — add an external ~10k to 3V3).

**Default pinout** (change any of these in the web UI):

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
**Chrome / Edge**, plug the ESP32 in over USB, click *Install*, and choose the serial port. One
firmware covers both motor types. (Source: [`../docs/rotor-flasher/`](../docs/rotor-flasher/), served
by the same GitHub Pages as the dashboard flasher.)

## Configure in the browser

After flashing, the rotor hosts an always‑on **`Rotor-setup`** Wi‑Fi AP (open). Join it and open
**`http://192.168.4.1/`** to:

- pick the **motor type** (28BYJ/ULN2003 or NEMA/STEP‑DIR) and set every **pin**,
- assign the **home switches** and optional **end‑stops** (+ active level),
- set the **ESP‑NOW channel** (0 = auto‑hunt Overhead's channel), and
- run **calibration** (CAL AZ/EL, SET NORTH, HOME, RESET) from buttons.

Saving writes to NVS and reboots to apply. The AP coexists with ESP‑NOW on the same channel, and while
you're connected the rotor holds its channel steady so the page stays reachable.

## Calibrate — no gear‑ratio math

Use the buttons in the **Rotor‑setup** web UI, or a serial monitor at **115200**. Results persist to
NVS and survive reflash.

| Command | What it does |
|---|---|
| `CAL EL` | Measures el **steps/deg** against gravity (the accelerometer) — automatic, exact. |
| `CAL AZ` | Homes to the limit switch, jogs one full turn back to it → az **steps/deg** = steps/360. Falls back to a guided `MARK` cal if cable‑wrap blocks a full turn. |
| `SETNORTH` | Jog the pointer to true north first, then this stores the north offset. |
| `AZ± <deg>` / `EL± <deg>` | Jog an axis (used for `SETNORTH` and the manual az fallback). |
| `MARK` | Manual az cal: send at a reference mark, jog 360° back, send again. |
| `SHOW` | Print current calibration. |
| `HOME` | Re‑home. |
| `RESET` | Clear NVS back to factory defaults (web RESET reboots to apply). |

Typical first run: `CAL EL` → `CAL AZ` → jog to north → `SETNORTH`. Done.

## Deferred (not in this firmware)

The Overhead **sender** side (broadcasting `telemetry_t`), the repo relocation, and the **radio /
Doppler** node are deferred — see §10 of the spec. `// DEFERRED:` markers sit at those seams.
