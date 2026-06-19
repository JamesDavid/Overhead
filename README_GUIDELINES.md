# README_GUIDELINES.md — how to write the project README

This file is **instructions for producing `README.md`**. It is *not* the README.
Follow it end-to-end; the goal is a README that makes a stranger want to clone the
repo, buy/wire the hardware, and flash it. Sell first, then teach.

When done, the README should let a reader (a) understand why this exists and why
it's worth the effort in ~60 seconds, and (b) see every screen and feature in
detail, with real screenshots. The "boring" setup lives in a separate linked file
(see §6) so the README stays a showcase.

---

## 1. Tone & framing

- Lead with the **elevator pitch**, not the build steps. Someone skimming should
  get hooked before they hit a single `git clone`.
- Confident but honest. It's a real, polished multi-tab dashboard — show that with
  screenshots — but don't oversell precision (the astronomy is "good enough for a
  glanceable dashboard," not an ephemeris).
- Second person, active voice ("Tap the centre to cycle views"), short paragraphs,
  lots of images.

## 2. Required README structure (in order)

1. **Title + one-line tagline** + a hero screenshot (pick the most striking page —
   the Star Map mid zoom-tour, or the Solar System sky-dome with the star overlay).
2. **Elevator pitch** (§3).
3. **Why this instead of cloning X** (§3) — the differentiators.
4. **Feature tour, screen by screen** (§4) — the bulk of the doc.
5. **Cross-cutting features** (§5) — things with no single screen.
6. **Get it running** — a short teaser + a link to `HARDWARE_SETUP.md` (§6).
7. **Technical challenges — overcome & still open** (§6a).
8. **Feature backlog / ideas** + link to the full backlog (§6b).
9. **Status / license / credits.**

## 3. The pitch (write this section to hit these points)

- **The mission / heart (lead with this emotionally):** beyond being the best
  air-&-space *desk clock*, it exists to **inspire kids** — to put the awe of
  far-away missions, satellites passing overhead, rockets launching, rovers on Mars,
  and the night sky **on a child's bedside table**, updating in real time. It turns
  "space is far away and abstract" into "look — the ISS goes over *our house* in 4
  minutes." Write the pitch so a parent/teacher/maker feels that, not just an
  engineer. (This also justifies content-rich, wonder-first features like a missions/
  rover page, the constellation tour, and the orrery.)


- **What it is:** a clean-room, modular **ESP32 "Cheap Yellow Display" (CYD)
  situational-awareness dashboard** for the sky — satellites, launches, aircraft,
  aviation weather, space weather, the solar system, a live star map, and a
  tonight-at-a-glance agenda — on a $10–15 touchscreen.
- **Why it's cool:** it's a *whole observation deck* on a cheap device — it knows
  where you are and what's overhead *right now*, and an "Intelligent Focus"
  director auto-surfaces the thing worth looking at (an imminent ISS pass, a
  launch, a geomagnetic storm) across tabs.
- **Why we wrote it instead of cloning something:** most CYD projects are
  single-purpose (one weather screen, one clock). This is a **modular multi-page
  app framework** (a page carousel + a provider/scheduler/event-bus core + a
  cross-tab director) engineered to **run real HTTPS data feeds on a no-PSRAM
  device** — which is the hard part everyone else avoids. Call out the
  engineering: serialized non-blocking net task, heap-floor-aware TLS, stale-serve
  resilience, a WiFi watchdog, and a full **remote debug/automation API**
  (`/remote` live screen + tap/swipe + screenshot) that almost no hobby firmware has.
- Name the alternatives fairly (generic CYD weather stations, phone apps like
  Heavens-Above / SkySafari, desktop orreries) and say what this does that they
  don't: *always-on, glanceable, location-aware, multi-domain, on dedicated cheap
  hardware you can leave on a shelf.*

## 4. Feature tour — screen by screen (screenshot EVERY page and sub-view)

For **each** page: embed the screenshot(s), then explain **what it shows, what's
cool, and how to operate it** (which taps/swipes do what). Capture **every
sub-view** (most pages cycle sub-views on a centre tap). The carousel order and the
sub-views to capture:

0. **Agenda** — the "tonight at a glance" home. Sky Window 24 h timeline (darkness/
   twilight shading, cloud heat band, moon-up band, event tick-marks + legend),
   the Today/Tonight context title, the "clear & dark window" verdict, the
   meteor-shower line, and the Upcoming list (passes with AOS+LOS, launches,
   sun/moon rise-set). Note: tap an event to jump to its tab.
1. **Launches** — two sub-views (centre-tap toggles): **Card** (next launch:
   provider, vehicle, mission, pad, country, status pill, big `T-` countdown,
   upcoming list) and **Map** (world map with a marker at every upcoming launch
   site; side-tap cycles rockets). Capture both. Show the **site** and **company**
   filter chips.
2. **Aircraft** — N-up ADS-B radar with 5/10 nm + range reference rings, heading
   chevrons, tap-to-select a blip. Info column: callsign, type, altitude, GS/track,
   distance/bearing, **look az/el**, squawk (decoded). Capture: normal radar; a
   selected contact; the **emergency-squawk** banner if you can stage one; the
   centre-airport chips and the range/ground/**alt**/**category** filter chips.
3. **Aviation weather** — five sub-views (centre-tap cycles): **Map** (flight-
   category dots + wind vectors + **airport id labels** + zoom), **METAR** (decoded
   card with °F/mph/inHg + raw), **TAF** (decoded periods), **Sounding** (Skew-T
   temp/dewpoint vs ft, freezing level, winds-aloft, dry-parcel line, soaring
   analysis), **Hazards** (AIRMET/SIGMET/PIREP in plain language). Capture all five.
4. **Satellites** — three sub-views (centre-tap): **Polar** (sky-dome az/el with
   the predicted **pass trajectory arc**, AOS/LOS times, live Doppler for FM birds),
   **Ground** (world ground-track), **Graph** (elevation-vs-time pass profile).
   Capture all three; note the min-elevation filter chip + watchlist.
5. **Space Wx** — Kp gauge + **history sparkline**, SFI, GOES flare class, solar-wind
   speed + IMF Bz, **aurora chance** for your geomagnetic latitude, and the HF band
   condition table. One screenshot.
6. **Solar System** — four sub-views (centre-tap): **Sky-dome** (Sun/Moon/planets by
   az/el, moon phase, **naked-eye visibility** rating, **rise/transit/set**, closest
   **conjunction**, optional star overlay), **Orbits** (top-down orrery incl. minor
   bodies/Starman), **Jupiter** (Galilean moons strung along the equator, *tilted to
   your sky*), **Saturn** (rings *tilted to your sky*). Capture all four.
7. **Star Map** — all-sky azimuthal chart: ~80 named stars, constellation lines,
   **Messier deep-sky markers**, ecliptic, Sun/Moon/planets overlay. Capture: the
   full chart; a **tap-to-zoom** region (shows all star names + the constellation
   name); a frame of the **auto-tour** (zoomed on a constellation with its
   brightest-star subtitle). Note the magnitude-limit + SS-overlay + tour badges.
8. **Health** — system table (WiFi, heap + **heapBlkMin** + **httpsSkip**, FS,
   uptime, location), per-provider status + age, display-mode + brightness cycles,
   Refresh / Recalibrate / two-tap Reboot. **(SSID appears here — redact it, §4b.)**

### 4a. How to capture the screenshots
- Use the remote loop (see `PIO_DEBUG.md`): `scripts/shot.ps1` writes `.pio/shot.jpg`.
- Navigate with `GET /api/swipe?dir=left|right` (page) and `GET /api/tap?x&y`
  (centre-tap ~x=160,y=110 cycles a page's sub-views). Confirm the page with
  `/api/status` (`"page"`).
- **Pin the page first** so the Director doesn't switch mid-capture: tap the status
  strip to MANUAL, or long-press to pin. Set the **Day theme** (Health → Display)
  for bright, legible shots.
- Let data load before shooting (watch `/api/status`: `adsb`, `tle`, `kp`, `avwxN`).
- Save shots under `docs/img/<page>-<subview>.png`; reference them from the README.

### 4b. PII redaction — DO THIS, and restore afterward
- **Record originals first:** `GET /api/status` and save the real `locName`, `lat`,
  `lon` (and `tzOffset`). You will restore these at the end.
- **Move the device to a cool location for the shots** (so geolocation isn't your
  home). Use a launch site — **Cape Canaveral (28.49, −80.58)**, **Starbase
  (25.997, −97.155)**, or **Vandenberg (34.74, −120.57)** — it also makes the
  Launches/Aviation pages more interesting. Set it via the settings API
  (`POST /api/settings`; check `Settings`/`LocationService` for the manual-location
  keys — set name + lat + lon and disable IP-geolocation override), then **Refresh
  providers** (Health) or reboot so every location-derived page recomputes. Verify
  with `/api/status`.
- **Capture all screenshots** at the cool location.
- **Restore the original location** (`POST /api/settings` with the saved values) +
  refresh/reboot. Verify `/api/status` shows the original `locName` again.
- **Redact the SSID** (and optionally the LAN IP / mDNS host) from the **Health**
  screenshot with a solid box in an image editor before committing it. Never paste
  raw `/api/status` JSON containing the SSID into the README.

## 5. Cross-cutting features to describe (no single screenshot)

Document these in prose (they're selling points):
- **Intelligent Focus director** — the only cross-tab brain: an ambient resting tab
  by day/night with a multi-page attract tour, plus interrupts that seize focus for
  an imminent pass / launch (and badge notice pages for Kp storms / SPECI). In AUTO
  it switches the carousel (with a brief **"▸ <page>" auto-switch banner**); in
  MANUAL it just badges the tab.
- **Modes:** AUTO / MANUAL / pinned; tap the status strip to toggle, long-press to
  pin; MANUAL falls back to AUTO after inactivity.
- **Theming:** Day / Night (dark) / Night (red dark-adapt) palettes + brightness,
  auto by sun or forced from Health.
- **Remote control / debug API:** `/remote` (live screenshot + click-to-tap + swipe
  in a browser), `/api/screen.jpg`, `/api/tap`, `/api/swipe`, `/api/status`,
  `/api/settings`, ElegantOTA at `/update`. Mention `scripts/ota.ps1` +
  `scripts/shot.ps1`.
- **Reliability on a $12 no-PSRAM board:** serialized non-blocking net task,
  heap-floor-aware TLS (serves stale instead of OOM), stale-data clearing, a **WiFi
  watchdog** (auto reconnect/reboot), and live memory-pressure telemetry
  (`heapBlkMin`, `httpsSkip`). This is a real differentiator — say so.
- **Provisioning:** WiFiManager captive portal on first boot; IP geolocation;
  NTP/RTC time.
- Anything else present in `src/` at writing time — skim the pages/providers and
  describe every user-facing capability, even minor chips/toggles.

## 5a. Web-interface walkthrough (place AFTER the on-device tour, before §6a)

After the on-device screen tour, add a section showing the **web/remote interface**
(it's a real differentiator and part of the day-to-day experience). Screenshot it
if you can (it's a desktop browser — easy to capture cleanly); if a feature can't be
screenshotted, describe it. Cover:
- **`/remote`** — the live screen mirror with click-to-tap + swipe buttons (great
  hero image of "control your desk clock from a laptop").
- **`/update`** — ElegantOTA firmware upload.
- **`/` settings form** — what's configurable (location, watchlist, refresh
  intervals, theme, etc.). **Note the current form is basic** — if the planned
  settings revamp (map geolocation picker, satellite/celestial checkbox pickers,
  editable mDNS name) has landed, screenshot the new UI; otherwise describe the
  current form and point at the backlog item.
- **`/api/status`** — the JSON health/telemetry (mention `heapBlkMin`/`httpsSkip`),
  but **do not paste raw JSON containing the SSID** (redact, per §4b).
- Capture web screenshots while the device is at the **redacted launch-site
  location** too, and restore afterward (same §4b procedure).

## 6. Link to the hardware/setup guide (separate file)

End the README with a short "Build one" teaser and a link to **`HARDWARE_SETUP.md`**
(create that file separately; it holds the "boring" detail so the README stays a
showcase). That file must contain:
- **Supported targets** and their PlatformIO envs: `cyd28_ili9341` (2.8" CYD,
  verified), `cyd4_st7796` (4"), `crowpanel_s3_5hmi` (ESP32-S3). Note which are
  verified vs WIP.
- **BOM per target** — the board itself (ESP32-2432S028R etc.), USB cable, optional
  RTC/GPS, power. Links/part numbers where possible.
- **Clone + toolchain:** install PlatformIO, `git clone`, the venv `platformio.exe`
  invocation (it's not on PATH — see `PIO_DEBUG.md`), the pioarduino esptool fix for
  the S3 env, build, first USB flash, then WiFi provisioning.
- **Flashing & OTA** (USB first time, then `scripts/ota.ps1`), and the **CYD quirks**
  cross-link to `CYD-ESP32-2432S028R.md` (display rotation, backlight, the JPEG colour
  readback, no-PSRAM budget, OTA/WiFi recovery).
- Troubleshooting: unresponsive-device serial reset, OTA flakiness, the heap floor.

## 6a. "Technical challenges — overcome & still open" section

Near the bottom (it's catnip for the engineering-minded reader deciding to build
it). Two short subsections, honest and specific — pull the real details from
`CYD-ESP32-2432S028R.md`, `PIO_DEBUG.md`, and the git history:

- **Overcome** — e.g.: running real HTTPS feeds on a **no-PSRAM** board (the ~42 KB
  contiguous-TLS floor; serialized non-blocking net task; serve-stale-instead-of-OOM);
  the **JPEG screenshot colour readback** (byte-swap + hi5=B/mid6=R/lo5=G, JPEGENC
  wants B,G,R); backlight via direct LEDC (LovyanGFX Light_PWM didn't drive GPIO21);
  un-mirroring the panel (rotation 6); anti-flicker without a framebuffer
  (startWrite batching + redraw-on-change); the **stale-data starvation loop** and
  its fix; observer-relative astronomy (parallactic-angle planet tilt, pass arcs).
- **Still open / known limitations** — e.g.: the TLS floor still starves fetches
  when `heapBlkMin` dips (can't shrink mbedtls buffers on the precompiled classic
  platform); the 16 KB debug-screenshot buffer eats headroom; OTA flakes under
  AsyncTCP load; occasional WiFi drops (now watchdog-recovered); Schlyter
  astronomy is ~arcmin, not an ephemeris; flicker on dense full-redraw pages; data
  caps (TLE watchlist-only, aircraft 24, METAR 12). Be candid — it builds trust.

Keep it skimmable (bulleted), link each claim's detail to `CYD-ESP32-2432S028R.md` where it
exists.

## 6b. "Feature backlog / ideas" section (very end, before status/license)

A short, enticing bulleted **highlights** list of what's planned/possible (a few of
the juiciest: e.g. weather trend analysis, surface-fronts map, nearest-airport +
frequency, Saturn's moons, bigger star catalog, on-device watchlist editing,
banner+buzzer alerts, hardware targets). Keep it to ~8–12 teasers — it signals the
project is alive and invites contribution.

Then **link to the full backlog**: `[BACKLOG.md](BACKLOG.md)` — say that's the
complete, living list. Do not duplicate the whole backlog in the README; the README
shows the highlights, `BACKLOG.md` is the source of truth.

## 6c. License (for the README's license section + a LICENSE file)

Goal: **most restrictive license that still lets hobbyists clone, build, run, and
modify it freely — but blocks anyone from commercially reselling this or
derivatives without a written license.** I.e. no free-riding for profit.

- **Recommended: PolyForm Noncommercial 1.0.0.** It permits any *noncommercial* use
  (personal/hobby/research/education), including modifying and sharing, but reserves
  all *commercial* use to the author (a separate written/paid license is required).
  It's a clean, purpose-built, source-available license — exactly this intent.
- Alternatives to mention only if needed: PolyForm Strict (no derivatives — too
  tight, blocks hobbyist mods, avoid); BUSL-1.1 (commercial-after-a-date / change
  date — more complex, for when you want eventual open-sourcing); CC-BY-NC is for
  content, **not** software — don't use it for the code.
- Add a real `LICENSE` file with the chosen text and a one-paragraph plain-English
  summary in the README ("Free for personal/non-commercial use and tinkering;
  commercial use or reselling — of this or derivatives — needs written permission;
  contact <…>"). Note third-party components keep their own licenses (LovyanGFX,
  ArduinoJson, SGP4, JPEGENC, ElegantOTA, ESPAsyncWebServer, etc.) — list them.

## 7. Before committing the README

- Every page **and sub-view** has at least one screenshot; images render; paths
  correct.
- SSID redacted everywhere; geolocation is a launch site in the shots; **device
  location restored** to the original.
- The pitch answers "why not just clone something else?" explicitly.
- The hardware/clone detail is in `HARDWARE_SETUP.md`, linked — not inline.
