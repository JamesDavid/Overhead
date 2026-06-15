# Overhead — Multi-Tab CYD Sky Dashboard

**Spec / handoff document.**
Target device: 4" Cheap Yellow Display (ESP32-32E, 480×320 ST7796, resistive touch)
Approach: **fresh, modular, clean-room build** — existing projects are *reference
material only*, not a fork base.

> This file is the project spec as originally handed off. The running status
> (what's built per milestone) lives in `README.md`; deferred polish lives in
> `BACKLOG.md`.

---

## 0. One-paragraph summary

A from-scratch tabbed "what's overhead / around me" dashboard for the 4" CYD.
Content pages are ordered by **layer — ground to deep space**, so swiping right
travels outward: a **Today/Tonight agenda** (home), **rocket launches**,
**aircraft** (ADS-B), **satellites** (ISS / AMSAT / custom), **space-weather /
HF-propagation**, **solar system** (Sun / Moon / planets), and a
**constellations / star map** [stretch]. Settings, a system-health view, and
location all live behind status-bar corner glyphs rather than eating tab space. An
**Intelligent Focus** director auto-surfaces the most relevant tab — imminent
satellite passes and launches seize focus, and the device defaults to sky mode
(and a red dark-adapt theme) at night. The design goal is a clean layered
architecture: a hardware abstraction layer, shared services (time / location /
network / cache, with WiFiManager provisioning + ElegantOTA), a reusable astronomy
compute core, data **providers** (one per source), thin UI **pages** that
subscribe to providers, and a **Director** that decides what to show. No inherited
legacy code.

This is a personal/hobby build. Optimize for clean, efficient, fun-to-extend —
not production hardening.

---

## 1. Design philosophy

- **Clean-room, not forked.** Borrow *ideas and algorithms* from existing projects
  (§12) and depend on a couple of well-scoped libraries, but the skeleton, UI
  layer, and data layer are ours and consistent throughout.
- **Layered + modular.** Strict separation: drivers → services → providers →
  astronomy engine → widgets → pages → director → app shell. A page never makes
  an HTTP call or touches a display register directly.
- **Rendering-agnostic pages.** Pages draw against an abstract `Canvas`/`Renderer`
  interface, so the graphics library is a swappable decision (§3).
- **One provider per source.** Each source owns fetch + parse + cache + freshness,
  exposes a typed model + status + age, and notifies subscribers on update.
- **Shared compute core.** Coordinate/time math lives in one astronomy module used
  by the satellite, planet, sun/moon, and star-map tabs — written once.
- **One brain for attention.** The **Director** (§7) is the *only* component with
  cross-tab awareness; it reads providers + clock + Sun position and drives page
  switches. Pages know nothing about each other.
- **Non-blocking by construction.** Network + heavy parse run off the UI path from
  day one (FreeRTOS task, second core).
- **Runtime config, not recompile-to-change.** Location, enabled tabs, units,
  ADS-B mode, refresh intervals, and focus behavior are runtime settings (LittleFS
  + optional captive-portal/web config); `config.h` is build-time flags only.
- **The device must never fight the user.** Auto-switching always yields to manual
  input. Predictable > clever.

---

## 2. Hardware target

- **Board:** 4" CYD "ESP32-32E 4 inch display" (AliExpress search term),
  ST7796 480×320, resistive touch (XPT2046).
- **MCU:** ESP32-WROOM class. **Confirm whether this exact board has PSRAM** —
  several 4" CYD variants do not. Biggest constraint on the star-map/world-map
  tabs and on the rendering-stack choice (§3).
- **HAL requirement:** wrap display panel + touch controller behind a small
  hardware abstraction layer (panel init, pins, rotation, calibration). Isolating
  this keeps the app portable across CYD variants and other ESP32 display boards
  (each becomes a PlatformIO env — see §3.1).
- **I2C RTC (optional, investigate):** standard CYDs have no battery-backed RTC, but
  most expose a spare/extension header. If this variant frees two GPIOs for I2C, an
  external **DS3231** keeps correct time across power loss and offline boots (closes
  the time-integrity gap in §13). Make it a per-target capability flag; future board
  variants may have one onboard. Confirm the exposed pins for this board.
- Optional add-ons: passive buzzer (pass/launch alerts — ties into §7 notices),
  GPS module (auto observer location → portable/POTA use).

**First task for Claude Code:** bring-up sketch that inits panel + touch via the
HAL, runs touch calibration, and prints free heap + largest free block. We need
the real memory budget before committing to §3 and the heavy tabs.

---

## 3. Tech stack decision (the key call)

### 3.1 Framework & build
- **PlatformIO + Arduino-ESP32 framework.** Every library we want (SGP4,
  Ephemeris, ArduinoJson, LovyanGFX/LVGL) is Arduino-friendly. ESP-IDF is "more
  proper" but costs ecosystem time for no real gain here. Default to Arduino-on-PIO.
- **Multi-target from the start.** Structure as one PlatformIO project with **one
  `[env:…]` per board variant** (e.g. `cyd4_st7796`, later an RTC-equipped or
  different-panel board). Per-env `build_flags` select the right `hal/Board.h` +
  capability flags (panel/pins, PSRAM, RTC present, GPS present); shared code is
  variant-agnostic and reads those flags. Adding a new board = a new env, not a fork.
- **LittleFS** for caches + bundled assets (catalogs, fonts, map tiles).
- **ArduinoJson** with **streaming parse + document filters** — LL2 and ADS-B
  payloads are large; never load a full response into a `String`.
- **WiFiManager** for first-boot WiFi provisioning (captive portal, no hardcoded
  creds) + **ElegantOTA** for browser-based OTA firmware updates, both hosted on an
  **ESPAsyncWebServer** that also serves the runtime settings page. This is a
  wall-mounted always-on unit — OTA matters from day one.

### 3.2 Rendering foundation — **decide before building UI**
The app is ~half standard chrome (tab bar, lists, cards, settings forms) and
~half custom graphics (sat polar plot, az/el graph, ground track, radar, sky
chart, gauges). Two coherent options:

**Option A (recommended): LovyanGFX + a small in-house widget toolkit.**
Modern, efficient CYD graphics lib (better sprite/DMA + CYD support than
TFT_eSPI), immediate-mode. Build a tiny reusable widget set once (`TabBar`,
`ScrollList`, `Button`, `Toggle`, `Gauge`, `Card`); custom renders are
first-class. **Pros:** lean RAM/flash, full control, works without PSRAM.
**Cons:** you write the chrome; forms are more manual.

**Option B: LVGL 9 app shell + `lv_canvas` for custom renders.**
Free polished tabview/lists/forms/theming; custom tabs use `lv_canvas`.
**Pros:** slick chrome, little code. **Cons:** object-tree/style/buffer RAM
overhead, larger flash; comfortable mainly **with PSRAM**.

**Recommendation:** **A** for efficiency and because the app's identity *is* the
custom graphics — unless the board has PSRAM and you'd rather not hand-build
chrome, in which case **B** is fine. Pages draw against our abstract `Canvas`
either way, so it's reversible.

### 3.3 Concurrency model
- **Core 1:** UI — tab manager, active page render + touch, animations, and the
  Director tick (cheap, every few seconds).
- **Core 0:** a `NetTask` (FreeRTOS) owning all HTTP, JSON parse, cache writes;
  talks to UI via a queue + mutex-guarded model store.
- On-device math (SGP4, ephemeris, star transforms) runs on the UI tick; if a
  full-sky transform stutters, move it to a worker.

---

## 4. Architecture & module layout

```
src/
  main.cpp                  // setup(): HAL, services, NetTask, Director, app shell; loop(): UI tick
  hal/                      // Display, Touch, Rtc, Board.h (pins + capability flags)
  core/                     // Canvas, Renderer, Theme, App, Page, Director, Focus, EventBus, Scheduler
  services/                 // TimeService, LocationService, NetClient, Cache, Settings, WebPortal, Provisioning
  astro/                    // Time, Coords, SatEngine (SGP4), Ephem (MarScaper, GPLv3)
  providers/                // Launch, Tle, Aircraft, SpaceWx, Weather
  widgets/                  // TabBar, ScrollList, Gauge, Radar, Banner, ...
  pages/                    // Launches, Satellites, SolarSystem, StarMap, Aircraft, SpaceWx, Agenda, Health
  data/                     // star catalog, shower table, fonts, map, airport+freq subset (OurAirports)
config.h                    // build-time feature flags only
```

**Page interface** (rendering-agnostic, and focus-aware via `PreFocus`):
`onEnter(App&, PreFocus)`, `onExit`, `tick`, `onTouch`, `onData(ProviderId, Model)`.
Only the active page renders/ticks. Providers push models via the EventBus; the
Director also subscribes. Per-page dirty flags; full 480×320 repaints are slow.

### 4.1 Navigation & information architecture (small-screen)

Eight destinations don't fit a tap-friendly tab bar at 480×320 on resistive touch,
so navigation is split:

- **Content pages, ordered by layer (ground → deep space):** **Agenda** (home) →
  **Launches** → **Aircraft** → **Satellites** → **Space Weather** → **Solar
  System** → **Star Map**. Navigated by **horizontal swipe** (carousel) with a slim
  **page-indicator** strip. The Director drives the same carousel programmatically;
  tab badges render on the indicator. Disabled/stretch tabs drop out.
- **Quick-jump grid:** tap the indicator → full-screen 3×3 icon launcher.
- **Utility overlays (corner glyphs):** **Settings** (gear), **System/Health**
  (pulse), **Location** (pin) — modal overlays from anywhere, not in the rotation.
- **Status strip (always visible):** clock · WiFi/RSSI · AUTO/MANUAL + pin state ·
  the three utility glyphs.

**Resistive-touch rules:** ~44 px min targets; single-touch only (use +/- for
zoom); firm-press taps; swipe-vs-tap debounce. Calibrate on first boot, re-runnable
from Health.

---

## 5. Astronomy compute core (`astro/`)

Written once, used by four tabs + the Director:
- **Time** — Julian Date, GMST/LST, optional ΔT.
- **Coords** — RA/Dec ↔ Alt/Az with observer location + LST; horizon refraction.
- **SatEngine** — wraps SGP4: TLE → sub-sat lat/lon/alt, observer az/el, slant
  range, **range-rate** (→ Doppler), and a **sunlit** flag. Pass prediction:
  AOS/TCA/LOS, max elevation, duration.
- **Ephem** — wraps **MarScaper `Ephemeris`** (VSOP87 + ELP2000) → RA/Dec, Alt/Az,
  rise/set, distance, apparent diameter; Moon phase + % illumination; **Sun
  altitude**; **subsolar point + day/night terminator** (grayline).

**License:** MarScaper `Ephemeris` is **GPLv3** — confine it to `astro/Ephem`
behind `#define ENABLE_EPHEM` so a permissive build can drop it. Confirm the SGP4
lib's license (usually permissive).

---

## 6. Tabs — data + UI spec

Each tab = a **Page** + one or more **Providers** + optionally the **astro** core.

### Launches 🚀
- **Primary:** Launch Library 2 `GET ll.thespacedevs.com/2.2.0/launch/upcoming/?limit=10&mode=list`
  → name, `net`, `net_precision`, `status`, provider, rocket config, pad+location,
  mission. **Fallback:** RocketLaunch.Live `fdo.rocketlaunch.live/json/launches/next/5`.
- **Compute:** `net` → T-minus each tick; respect `net_precision`.
- **Refresh:** 30–60 min. **UI:** next-launch card + `ScrollList` of next ~5; stale badge.

### Satellites 🛰️
- **Source:** Celestrak GP groups → LittleFS (`amateur`, `stations`, optional `visual`,`noaa`).
- **Compute:** az/el, ground track, AOS/TCA/LOS; AMSAT birds get uplink/downlink +
  live Doppler from a transponder table. Source the active-bird list from AMSAT, not memory.
- **Refresh:** TLEs 6–24 h; math every second. **UI:** polar plot + az/el graph +
  world ground track + pass clock + selector; freq/Doppler for AMSAT. Grayline overlay (toggle).

### Solar System 🪐
- **No network.** Sun/Moon/planets alt/az (mark above-horizon), rise/set, Moon
  phase + % illumination, distances. **UI:** horizon half-dome + list + Moon-phase icon.

### Constellations / Star Map ✨ *(stretch)*
- **Bundled (PC-generated):** reduced bright-star catalog (~1,000–1,600 to mag ~5.5)
  + constellation lines. Prototype ~150 named stars first and **measure**. Risk:
  memory + render time, esp. without PSRAM.

### Aircraft Nearby ✈️
- **Two modes (runtime):** local feeder `http://<host>/data/aircraft.json`
  (readsb/tar1090); cloud airplanes.live/`api.adsb.one` v2, **1 req/s**, poll 2–5 s.
- **Schema:** `ac[]` {hex,flight,lat,lon,alt_baro,gs,track,squawk,category,seen}.
- **Compute:** bearing + great-circle distance; filter+sort by range; drop stale.
- **UI:** observer-centered radar (N-up, range rings) + heading chevrons + callsign/alt.
- **Likely frequency:** bundle a regional **OurAirports** subset; infer voice freq
  from altitude + phase of flight (GND/TWR/APP/CNTR/CTAF). Inferred best-guess.

### Space Weather / HF Propagation 📡 *(recommended)*
- **Source (NOAA SWPC, no key):** Kp + SFI/A-K. **Compute:** Kp/SFI/A/K + day-night
  HF band heuristic. **Refresh:** 15–30 min. **UI:** Kp+SFI gauges, band table, age.

### Today / Tonight Agenda 🗓️
A forward-looking, time-ordered view: pass agenda (watchlisted birds), launch
agenda, and a **Sky Window timeline** layering darkness/twilight + moon-up shading
+ cloud-cover heat strip + event markers — the "clear-sky clock," event-aware, with
a one-line "worth it?" verdict. Daytime ambient default.

### Utility — System/Health 🩺, Settings ⚙️, Location 📍
Corner-glyph modal overlays (not content tabs). Health: WiFi/RSSI, IP/mDNS, uptime,
heap, LittleFS, build, NTP status, per-provider status table, force-refresh /
recalibrate / OTA / reboot. Settings: runtime settings (§7.10) + OTA link. Location:
Auto(IP)/preset/GPS; presets added via the WebPortal (Open-Meteo geocoding).

### 6.7 Filtering & watchlists (cross-tab)
- **Watchlist (star/favorite):** per-object toggle + "watchlist only" view — the
  primary declutter tool.
- **Category filters:** per-tab quick chips from each provider's native fields.
- **Filters scope the Director, not just the list:** the same predicate that hides
  rows also gates interrupts. v1 priority: watchlist everywhere · min-elevation +
  group (sat) · altitude + category (aircraft) · hide-TBD + provider (launches).

---

## 7. Intelligent Focus (auto-switching director)

`core/Director` decides which tab to show based on context. Subscribes to the
EventBus, TimeService, and astro engine; drives `App::focus(...)`.

- **Ambient vs interrupt.** Ambient (resting) by time of day: night → Solar System /
  Star Map; day → Launches / Agenda. Interrupt (seizes then releases): imminent sat
  pass, launch in terminal count.
- **Triggers (defaults):** sat pass (AOS ≤ 5 min AND max el ≥ 20°, hold to LOS);
  visible ISS (sunlit + dark + clear, boosted); launch (T- ≤ 10 min); night sky
  (Sun < −12°); daytime; observing window (cloud trend); geomagnetic (Kp ≥ 5);
  aircraft alert (7700/7600/7500, off by default). Ham default: all radio passes ≥
  min el (sat needn't be sunlit). Clouds gate *visibility* bonus, not radio.
- **Scoring:** `base + urgency + viewability − penalties`; must beat current by a
  margin (hysteresis); in-progress outranks not-yet-started; loser raises a banner.
- **Attention state:** AUTO (Director owns screen) ⇄ MANUAL (any interaction;
  inactivity timer, default 90 s, returns to AUTO). **Pin** = stay (no auto-switch,
  no revert). Master AUTO/MANUAL toggle. **Tab focus badge** when a winning
  interrupt is suppressed (recent interaction / pin / MANUAL): color/glyph by kind,
  pulses when in-progress, optional lead-time, auto-clears when the window passes.
- **Anti-flapping:** min dwell, commit a pass until LOS, never re-trigger the same
  event, cooldown after release.
- **Notice before switching (configurable):** banner + optional buzzer chirp; modes
  `silent-switch | banner-then-switch | banner-only`. **CW announce** option (Morse
  bird name / "AOS" / "LCH").
- **§7.9 Day/night theme & dark adaptation:** separate `ThemeController`. Modes
  Auto | Day | Night; Auto flips on Sun altitude with hysteresis (→ Night at < −6°,
  → Day at > −4°). Night palette: plain dark or red dark-adapt. Backlight dims at
  night. A runtime `Theme` palette struct all widgets read — no hardcoded colors.
- **§7.10 Settings exposed:** focus on/off, ambient day/night tabs, pass lead, min
  pass el, all-radio vs visible, launch lead/filter, night Sun-alt threshold, notice
  mode, buzzer/CW, inactivity timeout, pin gesture, tab-badge style, observing-window
  alerts, per-trigger enables. Appearance: theme mode, night palette, theme Sun-alt
  threshold, night backlight.

---

## 9. Data sources, rate limits & caching

| Source | Auth | Limit | Poll | Cache TTL |
|---|---|---|---|---|
| Launch Library 2 | none | heavy throttle | 30–60 min | serve stale on 429 |
| RocketLaunch.Live | none | light (fallback) | on demand | short |
| Celestrak GP (TLEs) | none | be polite | 6–24 h | 24 h+ (offline) |
| airplanes.live / adsb.one | none | **1 req/sec** | 2–5 s | last frame |
| Local readsb/dump1090 | none | LAN | 1–2 s | last frame |
| NOAA SWPC | none | be polite | 15–30 min | until next poll |
| Open-Meteo (tz / clouds / geocoding) | none | generous | clouds 30–60 min | until next poll |

**Provider contract:** one in-flight request per host; never fetch on the UI tick or
a touch handler; always expose last-good model + age + status; any 4xx/5xx → keep
serving cache + show a stale badge.

---

## 10. Build order / milestones

0. Scaffold + bring-up (HAL, renderer, app shell, EventBus/Scheduler/NetTask, heap print).
1. Services + infra (Time, Location, NetClient, Cache, Settings, WiFiManager, WebPortal+OTA).
2. Astronomy core (Time + Coords + SatEngine + sunlit; Ephem behind flag).
3. Satellites tab (TleProvider + plots + selector + Doppler).
4. Launches tab (LaunchProvider + countdown + fallback + stale).
5. Aircraft tab (local-feeder first, then cloud; radar; OurAirports nearest-airport + freq).
6. Solar system tab (Ephem → horizon diagram + Moon phase + subsolar/terminator).
7. Intelligent Focus + theming (Director ambient + interrupts + notices + lifecycle; ThemeController).
8. Space weather tab (optional): SWPC gauges + Kp trigger.
9. Star map (stretch): tiny-catalog prototype → measure → expand.
10. Agenda tab + observability (WeatherProvider; Sky Window timeline; observing-window alert).
11. System/Health overlay.
12. Polish (Settings UI, persistence, inactivity dimming, final theming).

Each tab is independently shippable; the Director layers on top once its feeders exist.

---

## 12. Reference & dependencies

**Inspiration (study, don't fork):** `HB9IIU/ESP32-ISS-Tracker` (SGP4, pass
prediction, polar/az-el/ground-track renders, Open-Meteo tz, buzzer; MIT);
`ok5tvr/satelite_tracker` (AMSAT Doppler + radar + TLE cache);
`GuitarML/SpaceStationTracker` (LVGL ISS + captive WiFi).

**Dependencies:** SGP4 lib · MarScaper/Ephemeris (GPLv3) · LovyanGFX *or* LVGL 9 ·
ArduinoJson · LittleFS · WiFiManager · ElegantOTA · ESPAsyncWebServer · RTClib
(optional DS3231).

**Endpoints:** Launches `ll.thespacedevs.com/2.2.0/launch/upcoming/`,
`fdo.rocketlaunch.live/json/launches/next/5`; TLEs
`celestrak.org/NORAD/elements/gp.php?GROUP=…&FORMAT=tle`; ADS-B `api.adsb.one` v2 +
local tar1090 `/data/aircraft.json`; Space weather `services.swpc.noaa.gov/products/…`;
Open-Meteo tz/clouds + geocoding `geocoding-api.open-meteo.com/v1/search`; Airports
`davidmegginson.github.io/ourairports-data/airports.csv` + `airport-frequencies.csv`.

---

## 13. Cross-cutting concerns & robustness

- **Cold-start & location bootstrap:** power → WiFiManager AP portal → join → NTP →
  location (active preset, else IP Auto) → first fetch → time-appropriate ambient.
- **Time integrity:** seed from I2C RTC if present; else gate all astro on a valid
  NTP sync, persist last-known time, show "time not synced" rather than wrong positions.
- **Pass-prediction cost:** compute passes only for watchlisted (and currently-up)
  birds, lazily, capped working set.
- **Page-state convention:** loading / empty / error / stale as a shared widget.
- **Watchlist defaults:** seed ISS + a couple of FM birds so the Director is useful
  on first power-up.
- **OTA / web security:** ElegantOTA basic auth + settings password.
- **Settings schema versioning:** store `settingsVersion`, migrate on boot.
- **Sunlit / eclipse calc:** needs Earth-shadow (umbra) geometry, not just Sun-up.
- **Inactivity dimming:** dim backlight after N minutes, wake on touch.

---

## 14. Appendix — deferred ideas (not v1)

**Aviation weather tab (candidate — adds a level of detail beyond a generic
weather app):** METARs + TAFs for nearby/selected airfields, and **atmospheric
soundings** (Skew-T / lapse-rate, inversion height, lifted index, wind profile)
for glider/soaring + general aviation analysis. Sources: NOAA Aviation Weather
Center (aviationweather.gov data API — METAR/TAF, no key) and a RAOB/RAP sounding
source (e.g. rucsoundings.noaa.gov). Ties into the Aircraft tab (decode the field
near a selected aircraft) and gives the **Director** new triggers: highlight a
freshly-posted METAR/TAF, a **SPECI** (off-cycle special report — usually a
significant change), or a new sounding for the home field. UI: decoded METAR card
+ a compact Skew-T/“thermal” strip (cloud base, convective potential). Heavier
parse (soundings) and bundling an airfield list (reuse the OurAirports subset from
the Aircraft tab). Strong fit for the layered provider/Director model.

Ham (later): POTA spots tab; PSKReporter/RBN propagation spots; Home
Assistant/MQTT publish; ntfy push; deeper observability (transparency/seeing,
Bortle); true airspace polygons (OpenAIP/FAA). Other tabs: Sun & Moon detail;
meteor showers; Starlink train passes. iOS port (§14.1): the layered design makes
~60–70% (models, providers, astro core, Director, filtering) shareable; HAL +
renderer/UI are platform-specific (SwiftUI + MapKit). Out of scope: internet radio,
DX-cluster telnet, comet/asteroid ephemeris, live solar imagery, "visible during
launch" filter.
