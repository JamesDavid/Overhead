# Overhead

A from-scratch, modular tabbed "what's overhead / around me" dashboard for
ESP32 touch displays — agenda, rocket launches, aircraft (ADS-B), satellites,
space weather, solar system, and a star map, with an Intelligent Focus
"director" that auto-surfaces the most relevant tab. See the project spec for
the full design.

This repo is the **clean-room implementation** — existing CYD projects are
reference material only, not a fork base.

## Status — all milestones (0–12) implemented ✅ builds on all 3 boards

Every spec milestone is in and the firmware compiles for `cyd28_ili9341`,
`cyd4_st7796`, and `crowpanel_s3_5hmi`. Content tabs (carousel, ground→space):
**Agenda · Launches · Aircraft · Satellites · Space Wx · Solar System · Star Map ·
Health**, with the **Director** (ambient day/night + pass/launch interrupts +
AUTO/MANUAL/pin lifecycle + tab badges) and **ThemeController** (day/night palette,
red dark-adapt, backlight dim + inactivity dimming) on top. Services: NTP/RTC,
IP-geolocation, threaded NetClient, Cache, Settings (+ a web settings form),
WiFiManager provisioning, ESPAsyncWebServer + ElegantOTA.

Remaining work is **polish** (tracked in `BACKLOG.md`) and **on-hardware
verification** beyond the 2.8" CYD (the 4"/CrowPanel panels, and runtime behaviour
of the data tabs). Astronomy is permissive (Schlyter Sun/Moon/planets + Hopperpop
SGP4); GPLv3 Ephem stays behind `ENABLE_EPHEM`.

## Earlier milestone notes

First real content tab. `TleProvider` fetches the Celestrak `amateur` + `stations`
GP groups via the NetTask, caches them in LittleFS (offline-friendly), parses to a
TLE list, and publishes on the EventBus. `PageSatellites` renders an observer-centred
az/el **polar plot**, a live info panel (az/el, range, sunlit, next-pass countdown),
and **live Doppler** for FM birds from a small transponder table. The `App` shell now
has **swipe carousel navigation** with page-indicator dots (Satellites ↔ Diagnostics).
Watchlist defaults (ISS + SO-50 + AO-91) are seeded in Settings.

Deferred to follow-ups (noted): world ground-track map, az/el time graph,
group/min-elevation filter UI, watchlist filtering of the selector, AMSAT
mode/band sub-filters, and sourcing the live AMSAT transponder set.

⚠️ **Flash:** small boards (cyd28/cyd4) at ~88% of the 1.5 MB slot. A partition
rebalance (single app slot, or smaller LittleFS) is needed before the bundled
catalogs land (airports m5 / star map m9).

## Status — Milestone 2 (astronomy core) ✅ builds on all 3 boards

The shared compute core is in (`src/astro/`): `Time` (JD/GMST/LST/ΔT), `Coords`
(RA/Dec↔Alt/Az + refraction), `Sun` (permissive low-precision solar position —
drives both the satellite sunlit test and, later, the Director's day/night, with
no GPLv3), and `SatEngine` over the Hopperpop **SGP4** library (MIT): observer
az/el, slant range, range-rate→Doppler, sub-satellite point, a cylindrical-umbra
**sunlit** flag, and pass prediction (AOS/TCA/LOS). `Ephem` (Sun/Moon/planets) is
written but fully gated behind `ENABLE_EPHEM` (GPLv3, default **off**). A boot
self-test (`ASTRO_SELFTEST`) validates the core against a known ISS TLE over
serial.

⚠️ **Flash:** SGP4 pushed the 1.5 MB-app boards (cyd28/cyd4) to ~87%. Before the
bundled catalogs land (m3+), those boards will need a partition rebalance (single
app slot, or smaller LittleFS); the CrowPanel's 4 MB app slot is unaffected.

## Status — Milestone 1 (services + infra) ✅ builds on all 3 boards

Milestone 0 (HAL bring-up) and Milestone 1 (services + infra) are in. The base
platform is complete: HAL (display/touch), core plumbing (EventBus, Scheduler,
Theme, minimal App shell), and the service layer — Settings (LittleFS + schema
versioning), Cache, a threaded **NetClient** (HTTP on a core-0 FreeRTOS task,
callbacks marshalled to the UI thread), TimeService (NTP + sync gating + tz),
LocationService (IP geolocation default), WiFiManager provisioning, and a
**WebPortal** (ESPAsyncWebServer settings page + JSON API + **ElegantOTA**).

A **Diagnostics page** proves the full pipeline: boot → WiFi → NTP → IP
geolocation (background fetch on the NetTask) → EventBus → screen redraw, with
the settings/OTA UI live at `http://overhead-XXXX.local/`.

Flash headroom note: with the full infra the 4 MB boards sit at ~1.30 MB of the
1.5 MB app slot (~82%). The heavy tabs (SGP4 + bundled catalogs) will need
watching there; the CrowPanel's 4 MB app slot has plenty of room.

Still **compile-verified only — not yet flashed to hardware.**

## Targets (multi-board by design — spec §3.1)

One project, one renderer (**LovyanGFX**), one `[env:…]` per board. Shared code
is variant-agnostic and reads pins + capability flags from `src/hal/Board.h`,
selected by the env's `-D BOARD_*` flag.

| Env | Board | MCU | Panel | Touch | PSRAM | RTC |
|---|---|---|---|---|---|---|
| `cyd28_ili9341` *(default)* | 2.8" Cheap Yellow Display (ESP32-2432S028R) | ESP32 | ILI9341 240×320 SPI | XPT2046 resistive (own VSPI bus) | none | no |
| `cyd4_st7796` | 4" Cheap Yellow Display (ESP32-32E) | ESP32 | ST7796S 480×320 SPI | XPT2046 resistive (shared bus) | none | no |
| `crowpanel_s3_5hmi` | Elecrow CrowPanel Advance 5.0-HMI | ESP32-S3 | ST7262 800×480 parallel-RGB | GT911 capacitive | 8 MB | PCF8563 |

Pin sources: 2.8" CYD → `../BladeAir/cyd.md`; 4" CYD → [lcdwiki](https://www.lcdwiki.com/4.0inch_ESP32-32E_Display);
CrowPanel → `../cyd-radio/crowpanel-5in.md` (silkscreen + factory source —
authoritative over the Elecrow wiki, which lists a different/wrong pinout).

## Build / flash

```sh
# 2.8" CYD (default env — the board on hand)
pio run -e cyd28_ili9341 -t upload
pio device monitor                      # 115200

# 4" CYD
pio run -e cyd4_st7796 -t upload

# CrowPanel Advance 5.0-HMI
pio run -e crowpanel_s3_5hmi -t upload
```

> The CrowPanel env uses the **pioarduino** platform (arduino-esp32 3.x) because
> the S3 parallel-RGB `esp_lcd` driver LovyanGFX needs isn't exposed by the
> classic 2.0.x platform. If a build fails with `No module named 'esptool'`,
> install it into the PlatformIO venv: `…/.platformio/penv/Scripts/python -m pip
> install esptool` (a pioarduino dependency-bootstrap glitch, not a code issue).

On first boot the bring-up prints, over serial and on screen:
board name, panel resolution, **PSRAM size**, **free heap**, and **largest free
block**, then echoes live touch coordinates.

## Data generators

Bundled real-world datasets are produced by the scripts in
[`tools/`](tools/README.md) (run from the repo root) — airport + radio frequencies
(`gen_airports.py` → LittleFS `data/airports.bin`) and world map coastlines/borders
(`gen_worldmap.py` → flash `src/assets/Coastline.h`). See
[`tools/README.md`](tools/README.md) for how to refresh each, including pushing an
updated airport table to a running device over `/api/fs` (no reflash, non-destructive).

## Layout (spec §4)

```
platformio.ini          envs per board; build-flag-selected HAL
partitions.csv          4" CYD: dual-OTA 1.5MB + ~0.94MB LittleFS
partitions_16mb.csv     CrowPanel: dual-OTA 4MB + ~7.9MB LittleFS
include/config.h        build-time feature flags only (on the auto include path)
data/                   LittleFS image source (catalogs, fonts) — uploadfs
src/
  main.cpp              boot orchestration (cold-start sequence, spec §13)
  hal/
    Board.h             pins + capability flags per variant
    LGFX_Config.h       LovyanGFX panel/bus/touch wiring per variant
    Display.{h,cpp}     panel init, rotation, backlight, heap helpers
    Touch.{h,cpp}       XPT2046 cal (persisted) / GT911 (no cal)
    Rtc.{h,cpp}         optional RTC stub (per CAP_HAS_RTC; driver = m2)
  core/
    Ids.h               ProviderId
    EventBus.h          provider -> page/director pub/sub
    Scheduler.h         cooperative interval runner
    Theme.h             runtime palette (gTheme) every widget reads
    Page.h              page interface
    App.{h,cpp}         minimal shell: status strip + active page routing
    Canvas.h            abstract draw seam (filled in with the widget toolkit)
  services/
    Settings.{h,cpp}    LittleFS JSON store + schema versioning
    Cache.{h,cpp}       blob cache (body + fetchedAt + status)
    NetClient.{h,cpp}   queued non-blocking HTTP (core-0 NetTask)
    TimeService.{h,cpp} NTP + sync gate + tz offset
    LocationService.{h,cpp}  IP-geoloc default + presets
    Provisioning.{h,cpp}     WiFiManager captive portal
    WebPortal.{h,cpp}        async settings page + JSON API + ElegantOTA
  pages/
    PageDiag.{h,cpp}    milestone-1 pipeline-proof page
```

Pages currently draw via `Display::gfx()` + `gTheme`; the `core/Canvas`
renderer-abstraction + widget toolkit land with the first real tabs (m3+), so
the carousel/quick-jump/corner-glyph chrome (spec §4.1) is not built yet.

## ⚠️ Needs on-hardware verification

This bring-up was written from datasheets/reference docs and **has not been
compiled or flashed here**. Confirm on real hardware:

- **4" CYD** — if colours look inverted, set `-D CYD_INVERT_DISPLAY=1`. If the
  panel stays black, the ST7796 may want a different LovyanGFX panel variant.
- **CrowPanel** — the RGB **porch timings** in `Board.h` are generic 800×480
  values; if the image tears/rolls, reconcile against the Elecrow factory
  `esp_lcd` config. Also confirm the **I²C coexistence** of Wire (expander,
  I2C_NUM_0) and LovyanGFX's GT911 (I2C_NUM_1) on the shared SDA/SCL pins, and
  that function jumpers are set to MIC&SPK (`00`).
- **PSRAM** on the 4" CYD: the heap print is ground truth. If it reports PSRAM,
  flip `CAP_HAS_PSRAM` in `Board.h` and reconsider the rendering options.

## Open decisions (spec §11)

Notably: rendering stack is settled as **LovyanGFX for both boards** (one
renderer, custom graphics are the app's identity; reversible via `Canvas`).
The CrowPanel's PSRAM would also permit LVGL, but unifying keeps the design
clean. Remaining §11 items (ADS-B mode, AMSAT bird list, location/GPS, star
map in v1, license posture, focus defaults) are still open.
