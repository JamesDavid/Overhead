# Overhead

**An air-&-space situational-awareness dashboard for a $10 touchscreen — the sky, on your desk, updating in real time.**

| |
|---|
| ![The 2017 Great American Eclipse, recreated as a saved "memory sky"](docs/img/starmap-memory.jpg) |
| *A saved **memory sky** — the exact sky over Hopkinsville, KY at 18:25 UTC on 21 Aug 2017, the moment of greatest eclipse, with the Sun (note the corona ring), Regulus right beside it, and the planets strung along the ecliptic, recreated on the device.* |

---

## What is this?

**Overhead** turns a cheap ESP32 "Cheap Yellow Display" (CYD) into a glanceable
observation deck for everything happening above you — **right now, from where you
are.** Satellites passing overhead, rockets about to launch, aircraft on approach,
aviation and space weather, the solar system, a live all-sky star map, and a
"tonight at a glance" agenda — all on a $10–15 touchscreen you can leave on a shelf.

It knows your location and the time, and an **Intelligent Focus** director quietly
surfaces the one thing worth looking at across all of those tabs: an ISS pass in
four minutes, a launch window opening, a geomagnetic storm, a thunderstorm rolling in.

### The heart of it

Beyond being the best little air-&-space *desk clock*, Overhead exists to **inspire
kids**. It puts the awe of far-away missions on a child's bedside table and makes it
*real*: space stops being abstract and becomes "look — the ISS goes over **our house**
in four minutes," "that rocket launches **tonight**," "this is the exact sky from the
night you were born." A glowing window to the cosmos, always on, always current.

## Why this instead of cloning a weather-station project?

Most CYD projects do one thing — a weather screen, a clock, a single gauge.
Overhead is a **modular multi-page application framework**:

- A **page carousel** + a **provider / scheduler / event-bus core** + a **cross-tab
  director** — not a single screen, but a whole dashboard you can extend.
- Engineered to **run real HTTPS data feeds on a no-PSRAM device** — the hard part
  most projects avoid. A serialized non-blocking network task, heap-floor-aware TLS
  (serve stale instead of crash), stale-data resilience, and a WiFi watchdog.
- A full **remote debug / automation API**: a `/remote` browser page that mirrors the
  live screen with click-to-tap and swipe, plus `/api/screen.jpg`, `/api/tap`,
  `/api/swipe`, `/api/status`, OTA — almost no hobby firmware ships this.

Versus the alternatives: phone apps (Heavens-Above, SkySafari) and desktop orreries
are great but they're *not always-on, glanceable, and sitting on your shelf*. Generic
CYD weather stations are single-purpose. Overhead is **always-on, location-aware,
multi-domain, on dedicated cheap hardware.**

> **Honest scope:** the astronomy is "good enough for a glanceable dashboard," not an
> observatory ephemeris (low-precision Schlyter orbits, ~arcminute). It's a window to
> wonder, not a telescope mount controller (though it could drive one — see the backlog).

## ⚡ [**Flash it to your CYD right now, from your browser →**](https://jamesdavid.github.io/Overhead/)

No toolchain, no IDE, no clone — desktop **Chrome** or **Edge**, plug in the board over USB, **pick your CYD variant**, and click **Install** (~1 minute). Then join the `Overhead-setup` Wi-Fi to enter your network.

---

## Feature tour

Eight tabs — swipe left/right between them. Most pages **cycle sub-views on a centre
tap**; **up/down swipe** scrolls a list or steps sub-views; **side taps** step items
(a satellite, an aircraft, a launch). **Tap the page title** for a **views menu** that
lists the current page's sub-views by name so you can jump straight to one. Tap the
status strip to toggle **AUTO / MANUAL** (and **tap a Director alert banner to jump to
the page it's about**); long-press to **pin**.

### 0 · Agenda — *your night at a glance*

| |
|---|
| ![Agenda](docs/img/page0-agenda.jpg) |
| The **Sky Window**: a 24 h timeline that shades day → civil → nautical → astronomical darkness, paints a cloud-cover heat band across the top of each hour and a moon-up band along the bottom, and tick-marks every event with a legend. Underneath: the **clear-&-dark verdict** ("Clear & dark 02:35–04:35"), a **tonight's-sky** line of the naked-eye planets and the constellations with ≥3 stars up during the darkest hour (it even calls out an active meteor shower's ZHR), and a scrollable **Upcoming** list with cloud-% coloured per event. **Tap any event — in the timeline or the list — to jump straight to its tab and focus that exact item.** |

### 1 · Launches — *what's going up*

| |
|---|
| ![Launch card with the Director's launch alert + visibility verdict](docs/img/director-alert.jpg) |
| The next launch: provider, vehicle, mission, pad, country, a colour-coded **status pill** (Go / Hold / TBD), and a big `T-` countdown — precise launches tick `T-HH:MM:SS`, vague ones show `~ MMM DD`. The line **"see: 809 km W — faint (night, far)"** is the **visibility verdict**: Overhead works out whether *you* could see the launch (twilight-plume geometry, distance, cloud) and rates it likely / faint / unlikely. *(This frame also caught the Director's **"Launch in 6m"** interrupt banner in the status strip — the cross-tab alert in action.)* |

| |
|---|
| ![Launches map](docs/img/launches-map.jpg) |
| The **Map** (centre-tap toggles to it): a world map with a marker at every upcoming launch site. The selected pad gets a ring and a **launch-corridor azimuth arrow**, and a sight-line from your crosshair coloured by the visibility verdict. Side-tap steps launches; bottom chips filter by **time window** (24 h / 7 d / 30 d / all), **hide-TBD**, **visible-only**, **site**, and **provider**. |

### 2 · Aircraft — *who's overhead right now*

| |
|---|
| ![Aircraft radar](docs/img/page2-aircraft.jpg) |
| A north-up **ADS-B radar** with range rings (your range + 5/10 nm reference), per-blip **heading chevrons**, and **dead-reckoned** motion between updates — **including altitude**, so a descending contact's flight-level readout ticks down live between feed updates. Tap a blip → callsign, type, altitude, ground speed + track, distance/bearing, and the **look az/el** (where to point binoculars). An **emergency squawk** (7700/7600/7500) rings the contact and raises a banner, and a plane passing **nearly overhead** (within 5 nm and >20° up) fires a cross-tab "Overhead" alert + tab badge. HOME + airport chips recenter the radar; chips filter range / on-ground / altitude / category. |
| **The frequency marquee** along the bottom is a sleeper feature: it scrolls the **nearest airport's identifier, name, distance/bearing, and its actual radio frequencies** — ATIS/AWOS, CTAF/UNICOM, clearance, ground, tower, and approach/departure — pulled from a **bundled 3,877-field US airport database** baked into the LittleFS image (so it works without a network). Glance at it to know exactly what to dial up on a scanner to hear the traffic you can see on the radar. *(The cloud ADS-B feed runs over plain HTTP so it populates even under the no-PSRAM heap floor; the radar above is live traffic at a busy metro, with the home location redacted.)* |

| ![Aircraft activity board](docs/img/page2-aircraft-activity.jpg) |
|---|
| **Swipe up/down** for two more views. **Activity** is a live arrivals/departures board: every contact is classified — **departing** / **arriving** / **local** / **transit** (green climbing out, amber descending in) — relative to the nearest field, inferred from altitude + vertical rate, with flight, type, altitude (hundreds of ft), climb/descend arrow, and range. **Stats** totals the activity + category mix and calls out the **closest, highest, and fastest** contact. *(Live metro traffic above: departures and arrivals at KPHX, plus locals at KFFZ/KCHD and one en-route transit.)* |

### 3 · Aviation weather — *the sky's flight briefing*

| |
|---|
| ![Flight-category map](docs/img/aviation-map.jpg) |
| **Map** — the unified weather map. Every airport **dot is coloured by pressure** (inHg: blue high / grey normal / red low) and its **id by flight category** (VFR green / MVFR cyan / IFR-LIFR orange), so you read two layers at once. The top-left badge cycles which value rides beside each field — **category / cloud / wind / inHg** — and **zooming in** (tap a point) grows a **wind barb** on each dot plus a 2–3 line readout (id + cloud %, inHg, then wind kt). A **home** chip recenters on you at 1×; the scope badge cycles **200 mi / US / world** (tap the US/world map to drill into a region); dense areas auto-declutter and reveal more as you zoom. Your location is the green dot. |

| |
|---|
| ![Decoded METAR card](docs/img/aviation-metar.jpg) |
| **METAR** — a fully decoded card: Zulu + local obs time, wind (kt + mph), visibility, ceiling, temp/dewpoint (°C + °F), QNH (hPa + inHg), present-weather, and the raw text. Station chips jump between nearby fields. |

| |
|---|
| ![Skew-T sounding](docs/img/aviation-sounding.jpg) |
| **Sounding** — a Skew-T model profile: temperature + dewpoint vs altitude, the **freezing level**, **winds aloft** at 3/10/20/30 kft, a dashed dry-parcel line, and a soaring **stability analysis** (cloud base / top-of-lift / inversion). |

| |
|---|
| ![24h area trends](docs/img/aviation-trends.jpg) |
| **Trends** — 24 h area sparklines for temp / dewpoint / cloud / pressure with a plain-language conclusion ("pressure steady", "+ fog/low-cld risk", "clearing"). |

*(The old standalone synoptic "Pressure" view is now folded into the **Map** above — pressure is the dot colour at every zoom, with inHg as one of its layers.)*

Two more views appear **only when there's data**: a **TAF** view (decoded FM/BECMG/TEMPO/PROB
periods) when a field carries one, and a **Hazards** view (AIRMET/SIGMET/PIREP in plain
language) when an advisory is nearby. When there isn't, they drop out of the carousel —
and a hazard or **newly-forecast extreme weather** (thunderstorms, hail, heavy precip,
strong wind) is surfaced cross-tab as a notice instead (see the Director).

### 4 · Satellites — *catch the pass*

| |
|---|
| ![Polar pass view](docs/img/satellites-polar.jpg) |
| **Polar** — a sky-dome with the predicted **pass-trajectory arc** (solid above your min-elevation, dashed below), AOS/LOS markers + times, max elevation, sunlit-vs-eclipsed flag, live range, and — for FM birds — **live Doppler** uplink/downlink ("DL 145.800 −1.2k"). Counts down "AOS T-7h17m max 44°", and for the bright birds (ISS / Tiangong / Hubble) gives a **naked-eye verdict** — "✱ VISIBLE to eye ✱" when it'll be sunlit against a dark sky, or *why not* ("daylight" / "in shadow"). A row of **tap-chips** along the top jumps straight between your tracked birds. |

| |
|---|
| ![Ground track](docs/img/satellites-ground.jpg) |
| **Ground** — the world **ground track** for ~one orbit, the sub-satellite point (sunlit/eclipsed), the day/night-aware subsolar marker, and your location. Side-tap steps your watchlist; a chip cycles the **min-elevation filter**. Passes use SGP4 from cached TLEs — so it **works offline**. |

### 5 · Space weather — *is the Sun acting up?*

| |
|---|
| ![Space weather](docs/img/page5-spacewx.jpg) |
| A **Kp** gauge + a 3-day history sparkline, the solar flux index, GOES **X-ray flare class** (C/M/X), **solar-wind speed** and **IMF Bz**, an **aurora-chance** read for your own geomagnetic latitude (it computes the auroral-oval boundary from Kp), and an HF **band-conditions** table (80–10 m, day/night) from an SFI+Kp heuristic. |

### 6 · Solar System — *the neighbourhood tour*

| |
|---|
| ![Sky-dome](docs/img/solar-skydome.jpg) |
| **Sky-dome** — Sun/Moon/planets by az/el on a horizon dome, each with a **naked-eye visibility rating** (easy/twilight/washed/scope), rise/transit/set, the closest **conjunction** when bodies are <5° apart, and an optional constellation-line star overlay. |

| |
|---|
| ![Orbits](docs/img/solar-orbits.jpg) |
| **Orbits** — a top-down **orrery** at live heliocentric longitudes (√-scaled rings so Mercury isn't crushed by Pluto), with minor bodies/Starman, and each body's **orbital speed around the Sun in km/h and mph**. |

| |
|---|
| ![Moon](docs/img/solar-moon.jpg) |
| **Moon** — phase + illumination %, distance, and **near/far-side maps** with Apollo crewed, robotic, and 2024+ CLPS **landing sites**, plus the sub-Earth/sub-solar points. |

| |
|---|
| ![Mars](docs/img/solar-mars.jpg) |
| **Mars** — distance + light-time, a surface map with polar caps and Olympus/Marineris, the sub-Earth circle, and **rover sols** (Perseverance/Curiosity) with status. |

| |
|---|
| ![Jupiter](docs/img/solar-jupiter.jpg) |
| **Jupiter** — the four Galilean moons strung along the equator in Jupiter-radii, with the whole system **tilted by the parallactic angle to match your sky** ("field tilt N° for your sky"). |

| |
|---|
| ![Saturn](docs/img/solar-saturn.jpg) |
| **Saturn** — the disk + rings with the live **opening angle** ("rings 26° open, north face") and a Cassini-division hint, also tilted to your sky. |

| |
|---|
| ![Deep space](docs/img/solar-deepspace.jpg) |
| **Deep Space** — Voyager 1/2 & New Horizons distances in AU + light-hours, plus in-flight missions (Psyche, Europa Clipper, JWST, Parker). |

| |
|---|
| ![Meteor showers](docs/img/solar-showers.jpg) |
| **Meteor showers** — upcoming showers in date order with ZHR, days-to-peak, and a **radiant-quality** rating from your latitude (great/good/low/poor); active showers flagged "NOW". |

### 7 · Star Map — *the whole sky, and the night you remember*

| |
|---|
| ![Full sky](docs/img/starmap-full.jpg) |
| **Full sky** — an all-sky azimuthal chart from a real generated catalogue (HYG + d3-celestial): ~1500 stars, all 88 **constellation figures**, Messier **deep-sky markers**, the ecliptic, and the Sun (a small **corona ring** so it reads clearly without washing out the chart)/Moon/planets. |

| |
|---|
| ![Tap to zoom](docs/img/starmap-zoom.jpg) |
| **Tap to zoom** — tap any region and it magnifies: fainter stars fade in and star + constellation names appear, with a centre az/el readout. |

| |
|---|
| ![Constellation tour](docs/img/starmap-tour.jpg) |
| **Tour** — a badge auto-zooms each above-horizon constellation in turn (here, Andromeda), naming its stars, then moves on. Bottom badges cycle the **magnitude limit** and toggle the Sun/planets overlay + the tour. |

| |
|---|
| ![Memory sky](docs/img/starmap-memory.jpg) |
| **Memory skies** — save "the sky at *this moment* from *this place*" (a birthday, an eclipse) and it renders the **full** sky for that exact instant and lat/lon, captioned title + place (top-left) and date + coordinates (top-right). **Swipe up/down to cycle** between the live sky and your saved skies — the **view dots on the right edge** show where you are. Add/edit them in the web UI's **Memory Skies** tab. |

| |
|---|
| ![Natal chart on a memory sky](docs/img/astro-chart.jpg) |
| **Natal chart** — on a memory sky, the **chart** chip overlays the real computed **Sun / Moon / Ascendant** + planets in their (tropical) signs, plus the honest twist only an astronomy device can add: the **Sun's actual constellation** right now ("Sun's stars now: Sagittarius") — which differs from the astrological sign because of precession. Astronomy first, with astrology as the cultural lens. |

### 8 · Device Health — *under the hood*

| |
|---|
| ![Health](docs/img/page8-health.jpg) |
| The system table (WiFi, heap + **largest-block low-water** + **httpsSkip**, filesystem, uptime, location) and per-provider **status + age in d/h/m/s**, plus on-device controls: cycle the display palette (Auto/Day/Night/Red) and brightness, toggle remote screenshots, **mirror the screen** (for a reflected-HUD mounting) and **invert colours**, toggle the **Web** server (off frees ~35 KB of contiguous heap for the feeds — it boots off by default and re-enables here or via the serial console), **Refresh** all providers, **Recalibrate** touch, and a two-tap **Reboot**. *(WiFi SSID and LAN IP boxed out here.)* |

---

## Cross-cutting features

These have no single tab — they're part of the whole experience.

| |
|---|
| ![Clock-mode overlay](docs/img/clock-overlay.jpg) |
| **Clock-mode overlay** — tap the time and a big clock stamps over the live page (static lower-right on data pages, corner-hopping on calm ones for burn-in); chips toggle 24 h/AM-PM and digital/analog, with a date complication. |

| |
|---|
| ![3x3 quick-jump grid](docs/img/grid.jpg) |
| **3×3 quick-jump grid** — tap the page dots and every tile shows a live micro-status (next launch T-, ISS AOS, nearest METAR, Kp, constellations up…); tap a badged tile to land on its alert. |

| |
|---|
| ![Red dark-adapt theme](docs/img/red-theme.jpg) |
| **Day / Night / Red dark-adapt themes** — auto by sun altitude or forced from Health; the red palette preserves night vision at the eyepiece. *(Shown here on a memory sky.)* |

- **Intelligent Focus director** — the cross-tab brain. An ambient resting tab by
  day/night with a multi-page attract tour; **interrupts** that seize focus for an
  imminent **pass** ("ISS 45° in 3m · VIS") or **launch** ("Launch in 6m" — *the
  Launches shot above*); a plane passing **overhead**; and **notice badges** for Kp
  storms, aviation SPECIs, hazards, and extreme weather. In AUTO it switches the carousel
  (with a brief **"▸ page"** banner) and pre-focuses the exact item; in MANUAL it just
  badges the tab — and **tapping the alert banner jumps to the page it's about**. A *new*
  severe condition fires a one-shot cross-tab alert so a dangerous change gets noticed.
- **Modes & status-strip chrome** — AUTO / MANUAL / pinned. The right cluster is icons:
  **WiFi signal bars** (tap → Health), a **mode glyph** (play/pause/lock), and a
  **location crosshair** that opens an on-device **saved-locations picker**.
- **Offline field mode** — boot with no WiFi and **tap past the captive portal** to run
  on cached data (satellite passes, star map, orrery, agenda all work offline); the
  WiFi-down reboot watchdog is suppressed and the last-known location is reused.
- **Provisioning** — WiFiManager captive portal on first boot; IP geolocation; NTP/RTC. The portal
  also carries **mirror screen** + **invert colours** toggles, so a reflected-HUD mounting that's
  unreadable on the glass can be corrected from your phone before the device leaves setup. (The same
  two toggles live on the Health page and in the web **Appearance** settings; SPI panels apply them
  in hardware, the RGB CrowPanel in software at the framebuffer push.)

### Web / remote interface

- **`/remote`** — a live screen mirror in your browser with click-to-tap and swipe
  buttons. Control your desk clock from a laptop.
- **`/`** — a tabbed settings app: Location (Leaflet map + address geocode + "My
  location" + saved locations), Focus (per-page day/night tour checkboxes), Satellites &
  Bodies pickers, **Memory Skies** (map picker), Appearance, Aircraft, System.
- **`/update`** — ElegantOTA firmware upload. **`/api/status`** — JSON health/telemetry.
- Helper scripts: `scripts/ota.ps1` (flash over WiFi) and `scripts/shot.ps1` (screenshot).

### Reliability on a $12 no-PSRAM board

This is the real engineering story (and a genuine differentiator): a single serialized
non-blocking network task so only one TLS session ever exists; **heap-floor-aware TLS**
that serves stale data instead of running out of memory and crashing; stale-data release
to keep the heap clear; a **WiFi watchdog** that reconnects and, if needed, reboots; and
live memory-pressure telemetry (`heapBlkMin`, `httpsSkip`) you can watch in `/api/status`.

---

## Build one

It runs on a **$10–15 ESP32 "Cheap Yellow Display."** The full bill of materials,
toolchain setup, first flash, WiFi provisioning, OTA, and board-specific quirks live in
**[HARDWARE_SETUP.md](HARDWARE_SETUP.md)**.

### ⚡ Flash from your browser — no toolchain

Open the **[web flasher](https://jamesdavid.github.io/Overhead/)** in desktop **Chrome**
or **Edge**, plug the CYD in over USB, and click **Install** — it flashes over Web Serial
in about a minute, then you join the `Overhead-setup` WiFi to provision. (Source for the
page: `docs/index.html`.)

### Build from source

Install PlatformIO, `git clone`, build the `cyd28_ili9341` env, flash over USB once, then
join the `Overhead-setup` WiFi to provision. After that, OTA from your desk.

---

## Companion rotor — a pointer that slews to what's overhead

An optional alt/az pointer ([`rotor/`](rotor/)) physically tracks whatever the dashboard is
following. Overhead broadcasts az/el over ESP‑NOW and the rotor homes itself and follows. It's a
separate ESP32 firmware and supports both a 28BYJ‑48/ULN2003 build and NEMA 17 + STEP/DIR builds.

- The wire format is **shared in one place** — [`shared/telemetry.h`](shared/telemetry.h), included
  by both sides so it can't drift.
- **Self‑measuring calibration** replaces entering a gear ratio by hand: `CAL EL` (against gravity),
  `CAL AZ` (a full switch‑to‑switch turn), `SETNORTH` — persisted to NVS.
- Flash the default ESP32 build from the browser via [`rotor/flasher/`](rotor/flasher/); NEMA/custom
  builds compile from source.

Build, flash, and calibration steps: [`rotor/README.md`](rotor/README.md). (The Overhead **sender**
side that broadcasts the packet is deferred — see spec §10.)

---

## Technical challenges — overcome & still open

**Overcome** (details in [CYD-ESP32-2432S028R.md](CYD-ESP32-2432S028R.md) and
[PIO_DEBUG.md](PIO_DEBUG.md)):
- Running real HTTPS feeds on a **no-PSRAM** board — the ~42 KB contiguous-TLS floor; a
  serialized non-blocking net task; serve-stale-instead-of-OOM.
- The **JPEG screenshot colour readback** — empirically-derived byte-swap +
  hi5=B/mid6=R/lo5=G, written B,G,R for JPEGENC.
- Backlight via **direct LEDC** (LovyanGFX's `Light_PWM` didn't drive GPIO21).
- **Un-mirroring** the panel (MV=0 rotation 6) and matching touch calibration.
- Anti-flicker without a framebuffer (`startWrite` batching + redraw-on-change).
- The **stale-data starvation loop** (retained data pinned the heap floor) and its fix.
- Observer-relative astronomy: parallactic-angle planet tilt, pass-trajectory arcs.

**Still open / known limits** — candid, because it builds trust:
- The TLS floor still starves fetches when `heapBlkMin` dips (can't shrink mbedtls
  buffers on the precompiled classic platform). The **live ADS-B feed** is the most
  affected — it clears and re-fetches contacts frequently, and the 16 KB screenshot
  buffer eats the headroom it needs, so a busy radar is hard to *screenshot*. **Flash is
  ~99 % full** on the classic env.
- OTA flakes under AsyncTCP load (boot-settle ~20 s and retry); occasional WiFi drops
  (now watchdog-recovered).
- Schlyter astronomy is ~arcminute, not an ephemeris; data caps (TLE watchlist-only,
  aircraft 24, METAR 12); flicker on dense full-redraw pages.

---

## What's next (highlights)

A few of the juicier backlog items: **az/el rotor output** (drive a real or DIY antenna rotator) and an
**IMU handheld antenna-aim** mode; externalising the orrery body list to LittleFS (comets,
NEOs, dwarf planets); Saturn's moons; true WPC surface fronts (blocked on a data source);
aircraft flight trails; rover/APOD imagery on PSRAM boards; on-device watchlist editing;
banner + buzzer alerts; and verifying the 4" and ESP32-S3 hardware targets.

The complete, living list is in **[BACKLOG.md](BACKLOG.md)**.

---

## Status / license / credits

Active, single-developer project; the 2.8" CYD (`cyd28_ili9341`) is the verified target.

**License: [PolyForm Noncommercial 1.0.0](LICENSE).** Free for personal, hobby,
educational and research use — clone it, build it, run it, modify and share it. **Any
commercial use or reselling — of this or derivatives — needs written permission.** See
[LICENSE](LICENSE).

Built on excellent open-source libraries, each under its own license: **LovyanGFX**,
**ArduinoJson**, **Hopperpop SGP4**, **JPEGENC** (bitbank2), **ElegantOTA**,
**ESPAsyncWebServer** / **AsyncTCP**, **WiFiManager**. Data from NOAA SWPC & the Aviation
Weather Center, Open-Meteo, ip-api, Launch Library 2, CelesTrak, the HYG database,
d3-celestial, and Natural Earth.

*73 de KE7AQA.*
