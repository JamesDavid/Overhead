# Polish backlog

Deferred polish — cut from the first pass to keep momentum. Pick up later.
(Bugs/blocking work go in commits, not here.) Items shipped are removed; this list
is the *remaining* work as of the latest sweep.

## UX shell — desk-clock (SHIPPED via overlay, Jun 2026)
Built as a device-wide **clock-mode overlay** (tap the time in the status strip)
rather than a separate home page: a big clock stamped on top of the live page —
parked static lower-right on data pages (sat pass / aircraft / launches / agenda
keep running underneath), or hopped to a random corner every ~10 s on calm pages
for burn-in; sprite-rendered (no flicker/scramble), with 24h/AM-PM + digital/analog-
ball toggles inside the box and a date complication. Plus the **3×3 quick-jump
grid** (tap the status dots) where every tile shows a live micro-status (next AOS +
sat, T-minus + mission, nearest METAR, planets/constellations up, Kp, problems...),
word-wrapped. A per-page `clockKeepLive()` hint drives the static-vs-hop choice;
the Director still interrupts / auto-switches.

Not built (small, optional — promote later if wanted):
- A `PageHome` "Now" card surfacing the Director's single highest-scored item
  (pass / launch / clear-dark window) as a card instead of a tab jump.
- `restMode = observatory`: a faint live rotating star-map/orrery *behind* the clock
  at night (reuse the Star Map / orrery renderers) — the inspire-a-kid mode.
- Settings / Health / Location as corner-glyph overlays off the carousel (see M11).

## M0 — bring-up / HAL
- Verify 4" CYD (ST7796) + CrowPanel panels on real hardware (only 2.8" CYD verified).
- CrowPanel: confirm RGB porch timings, I2C-expander backlight, GT911 coexistence on hw.
- DS3231/PCF8563 RTC driver (currently a stub; NTP-only for now).

## M1 — services / infra
- core/Canvas + Renderer abstraction + reusable widget toolkit (pages still draw via Display::gfx()).
- Full app-shell chrome: 3x3 quick-jump grid, corner-glyph overlays (Settings/Health/Location).
- ~~Auth on the settings API~~ DONE — all `/api/*` + `/` + `/remote` are Basic-auth
  gated with the OTA creds (`otaUser`/`otaPass`, default admin/overhead); scripts pass
  `-u`. Caveat: Basic auth over plain HTTP (no TLS) is gating, not strong security.
- Open-Meteo timezone refinement (DST rules; currently fixed offset from IP).
- GPS location source (module optional).

## M2 — astro core
- Enable/validate Ephem (GPLv3, ENABLE_EPHEM) for full planet precision; Schlyter is
  ~arcmin (outer planets worse, no perturbations). deltaT refinement.
- Turn ASTRO_SELFTEST off in release builds.

## M3 — Satellites
- On-device watchlist editing (now editable via the web watchlist field; add an
  on-screen add/remove). Matching is case-insensitive CONTAINS (so "SO-50" finds
  "SAUDISAT 1C (SO-50)"); designations with no catalog-name token need the real name
  (AO-91 = "FOX-1B"). SatGus is fetched by NAME=SATGUS.
- Group filter chips + sunlit-only toggle; AMSAT mode/band sub-filters.
- Source the live AMSAT transponder set (not the hand seed in Transponders.h).
- Grayline / day-night terminator overlay on the ground track (needs Ephem subsolar).
- Doppler: uplink correction + tuning readout.
- **Az/El rotor output + DIY rotor.** Stream the tracked az/el over serial (and/or
  TCP) in a standard rotor protocol — **GS-232** ("Wxxx yyy") or **EasyComm II**
  ("AZxxx.x ELyy.y") — so it drives Hamlib/rotctld or a hardware rotator. Works for ANY
  az/el the device computes (sat pass, Moon/planet, even a launch look-angle). Plus a
  companion **ESP32 rotor firmware**: two 28BYJ-48 steppers on ULN2003 drivers (az +
  el), accepting the same protocol over serial/Wi-Fi, with limit/home + microstep
  ramping — a cheap build-your-own rotor. Keep the protocol the contract between the
  two so either end is swappable (commercial rotor or our build).
- **IMU handheld antenna-aim mode.** Wire an I2C 6-DOF/9-DOF IMU (e.g. MPU6050 6-DOF,
  or MPU9250/BNO055 9-DOF with magnetometer for absolute heading) to the CYD's exposed
  I2C, mount the whole thing on a handheld Yagi/arrow antenna, and add a "manual track"
  mode: read the device's current az (compass/yaw) + el (pitch) from the IMU and show a
  live steering cue (arrows / "up-left", a centered crosshair, or a bullseye) toward the
  satellite's computed az/el so the operator hand-aims the antenna through the pass.
  9-DOF preferred for true heading (6-DOF yaw drifts; needs a known start heading).
  Pairs with the az/el compute already used by the sat pass + the rotor-protocol item.

## M4 — Launches
- **Launch az/el look indicator (not a full pass).** A real az/el track like the
  satellite page needs a trajectory we don't have — modelling the whole ascent would be
  fabrication. Honest version: show the look DIRECTION (az = bearing to pad) + an approx
  max elevation = atan(~150km burnout / ground distance) -> "look WNW, up to ~10 deg",
  maybe a short rising arc on a mini sky-dome. A full animated pass would be a modelled
  ascent profile (label it as such if ever done).
- **Filter the Launches list to "possibly visible"** (visibility level >= faint from the
  observer) — a 5th bottom chip (needs the chip row re-layout).
- **Per-mission launch path/azimuth (PSRAM boards only):** the light `mode=list` feed
  carries no orbit, and `mode=normal` is ~37KB for 8 launches (blows the no-PSRAM TLS
  floor) — so the map shows a per-SITE corridor azimuth (approx) instead. On the
  CrowPanel (PSRAM) switch the fetch to `mode=normal`, parse `mission.orbit`, and draw a
  real per-mission azimuth arc: az = asin(cos(inclination)/cos(pad_lat)) from a
  representative inclination per orbit (LEO/ISS/SSO/GTO...).
- More filter chips: by provider/site/country. (Time window 24h/7d/30d/all + hide-TBD done.)
- Detail view on centre-tap (full mission text, window open/close, weather, image).
- RocketLaunch.Live fallback parser: verify pad/location/mission paths on a live 429.
- Streaming JSON parse off the UI thread for the larger detailed mode.

## M5 — Aircraft
Done: nearest-airport + full likely-frequency list as a scrolling bottom marquee
(tools/gen_airports.py -> LittleFS /airports.bin, 3877 US fields; services/AirportDB
scans on demand; refresh over /api/fs, no reflash); dead-reckon blips between ADS-B
updates; alt/category filter chips; hide-on-ground; range steps; recenter-on-nearby-
airport; tap-on-blip; emergency squawk.
- Callsign labels on the (unselected) radar blips.
- Local-feeder auto-discovery / mDNS; adsb.lol secondary source.
- Flicker: radar still clears its circle bbox each tick — per-blip erase or a PSRAM sprite.
- Airport dataset is US-only; for worldwide, regenerate gen_airports.py without the
  US filter (LittleFS has room) — watch scan time + the marquee string length.

## M6 — Solar System
The tab now tours the whole system via centre tap: sky-dome -> orbits -> Moon -> Mars
-> Jupiter -> Saturn -> Deep Space -> meteors (the old Missions tab folded in). Moon,
Mars, Deep Space, rise/set+transit, naked-eye visibility and the showers page are done.
- **Saturn's moons** (Titan/Rhea/Dione/Tethys) on the moons & rings view — needs its
  own satellite theory (Meeus ch.46) or a calibrated circular model. (Jupiter's
  Galilean moons + Saturn's ring-opening are done + verified vs Horizons.)
- **Expand + externalize the orrery minor-body list (LittleFS, like airports).** Add
  more bodies (comets — Halley/Encke/NEOWISE-class; more asteroids/NEOs; dwarf planets).
  Move the Keplerian elements out of SolarSystem.cpp into a LittleFS file
  (`/bodies.bin`) generated by a `tools/gen_bodies.py` from JPL SBDB
  (ssd-api.jpl.nasa.gov/sbdb.api) — real elements, refreshed by re-running the script +
  push over /api/fs (same flow as airports). A small loader (like AirportDB) feeds the
  orrery + minor-body selector.
  - **Self-update?** Possible for a small set: the device could fetch each body's
    elements from JPL SBDB occasionally (small per-body JSON) and cache to LittleFS.
    But it's HTTPS + JSON parse per body -> heap pressure on the no-PSRAM CYD (the TLS
    floor). So: gate device self-update to PSRAM boards; no-PSRAM uses the script
    refresh. Low-precision orrery elements drift slowly (fine for years), so frequent
    updates aren't needed either way.
- Deep Space distances are extrapolated from baked epochs + recession rates; a real
  JPL Horizons feed would keep Voyager/New Horizons exact (off by ~1 AU otherwise).
- Mars NASA rover-photo feed (api.nasa.gov, DEMO_KEY) doesn't load on the no-PSRAM
  board (HTTPS skipped under the TLS floor); rover sols are computed locally as a
  graceful fallback. Needs the heap headroom or a lighter status source.
- Moon/Mars upcoming-mission lines are hand-maintained + undated on purpose (lunar
  schedules slip); wire a real launch feed for dated upcoming Artemis/CLPS/Chang'e.

## Cross-cutting — rendering
- Anti-flicker via _needClear + in-place padded text + blip-erase. Next: optional LGFX
  sprite double-buffer on PSRAM boards (CrowPanel) for zero-flicker everywhere.
- Page-state widget (loading/empty/error/stale) shared component instead of ad-hoc text.

## M7 — Intelligent Focus + theming
- **Banner before an auto-switch** (silent | banner-then-switch | banner-only) + buzzer
  chirp / CW announce — the current cross-tab alert is the warn-coloured status strip;
  a proper content banner widget is still missing.
- Sun/Moon **transit (peak/culmination)** markers + wire sun/moon and visual-pass events
  as Director focus inputs (rise/set already surface as Agenda events).
- Eclipse / supermoon: small static table of upcoming dates surfaced in Agenda + a
  Director flag for an imminent eclipse.
- Scoring with urgency/viewability bonuses + hysteresis + anti-flap dwell/cooldown
  (current Director is simple priority: pass > launch > ambient tour + notice pages).
- Observing-window banner ("clearing 22:00-00:30"). (Cloud-gating a visual pass ->
  "VIS(cloud)" when overcast is done; AUTO now also rotates through badged notice
  pages and jumps to a SPECI.)
- Pulsing/lead-time tab badges (currently a steady warn dot); aircraft-emergency trigger.
- Tune the day / red dark-adapt palettes per-panel (red toggle + brightness now on Health).

## M8 — Space Weather
- A/K indices + sunspot number; short Kp history sparkline (NOAA or hamqsl XML).
  (Kp, SFI, aurora-from-Kp+geomag-lat, GOES X-ray flare class, solar-wind speed + IMF Bz
  are done.)
- hamqsl.com band-condition XML as a richer alternative to the local HF heuristic.
- Kp/flare trigger currently only badges Space Wx; optional banner/auto-switch (m7).

## Aviation weather tab
Done: airport map (category dots + wind vectors + zoom + raw METAR), decoded METAR card
(°F/mph/inHg parens, Zulu/local obs time), decoded TAF periods, Open-Meteo sounding
(Skew-T temp/dewpoint vs ft, FZL, winds-aloft at altitude, dry-parcel line, soaring
analysis: stability / cloud base / top-of-lift / inversion), AIRMET/SIGMET + PIREP
hazards, SPECI Director badge. Remaining:
- Home-field pin / favourite; nearby-field flight-category strip.
- Pressure trend (SLP rising/falling) from the METAR (needs successive-METAR history).
- Proper Skew-T skew + isotherms; lifted index; wind barbs (vs the text winds).
  (AIRMET/SIGMET now decode to plain phrases; winds-aloft sit at their altitudes.)
- Optional: decode TAF cloud layers as a forecast ceiling timeline (Open-Meteo stays the
  primary cloud source for the Agenda Sky Window — TAF is airport-only + coded).

## M9 — Star Map
- **Personal / memory skies. — DONE (Jun 2026).** A saved `memorySkies` array of
  {title, place, epoch, lat, lon}; each renders the full star map (stars, figures,
  deep-sky, Sun/Moon/planets) for that instant + place as a Star Map sub-view, captioned
  title+place top-left / date+lat-lon top-right, cycled by up/down swipe (view dots show
  position). Added/edited in a new **Memory Skies** web tab with a Leaflet map picker
  (click/drag/geocode/"My location") mirroring Locations. The Director's ambient tour
  resets to the live sky. (Demo entries: 2000 millennium over Tempe; 2017 Great American
  Eclipse at greatest-eclipse.)
- **Build out the star + constellation database (real catalogs, generated). — DONE.**
  `tools/gen_stars.py` bakes `src/assets/StarCatalog.h` from real datasets (same flash-header
  pattern as gen_worldmap.py; re-run + reflash to refresh):
  - Stars: **HYG v41** -> `kStars[]` (name, raHours, decDeg, mag), brightest 1500 to mag 5.2,
    161 proper-named, `kStarMaxMag` emitted. Wide view filters to the mag badge; the renderer
    reveals the fainter tail up to `kStarMaxMag` as you zoom (faint stars skipped before
    projection, so the deeper catalogue is ~free in the wide view).
  - Figures: **d3-celestial** `constellations.lines.json` -> `kConLines[]` RA/Dec polylines
    (all 88; `kSkyBreak` raHours sentinel = pen-up) — direct draw, no fragile HIP/name lookup.
  - Labels: d3-celestial `constellations.json` -> `kCons[]` (name + label centre); `kDeepSky[]`
    a curated naked-eye Messier set. Consumers (PageStarMap lines/`conFocus`/labels/gridStatus,
    PageAgenda "tonight", PageSolarSystem sky-dome) refactored to polylines + label centres.
  - tools/README documents the workflow + knobs. (Stellarium `constellationship.fab` was the
    original plan but its URLs 404'd — d3-celestial polylines are cleaner anyway.)
  - Remaining stretch: personal/memory skies reuse this renderer; tiny flash fallback if absent.
- Pan/zoom (+/- buttons), magnitude limit persisted; gridlines / ecliptic. (Sun/Moon/
  planets are plotted; tap-to-zoom + zoom-reveal of fainter stars done; mag badge cycles.)

## M10 — Agenda + observability
- Sky Window: moon illumination shading intensity; precip overlay; finer (30-min) buckets.
  (Local-time labels on the +6/+12/+18 ticks done; tapping an event jumps to its tab.)
- Context title Today/Tonight by time of day.
  (Tonight's visible planets + constellations now replace the far-off meteor
  countdown; tapping an event pre-focuses the exact bird/launch — both done.)

## M11 — System/Health
- **Saved locations + easy switching. — DONE (Jun 2026).** Web-UI saved `locations`
  list (name + lat/lon via the Leaflet picker); on-device a status-bar **location
  crosshair** opens a modal picker (Auto/IP + each saved location, current selection
  shown + arrow-marked) that switches the active location via LocationService.
- **Status-bar chrome — DONE (Jun 2026).** Replaced the "WiFi -NN" text with a
  **signal-bars glyph** (bars by RSSI; slash when offline) that taps through to Device
  Health; a **mode glyph** (play=AUTO / pause=MAN / padlock=PIN) replaced the text tag;
  and a **location crosshair** opens the saved-locations picker (above). Spaced as
  distinct tap targets.
- Make Health a corner-glyph MODAL OVERLAY (with Settings + Location) once overlay chrome exists.
- Per-provider "next poll" + last HTTP status (need providers to expose them; /api/status
  now reports adsb/tle/avwx/kp/sfi for remote diagnosis).
- Toast after Refresh. (Two-tap Reboot confirm done.)

## Remote control / debug (shipped — tuning left)
- /remote = live full-res JPEG + click-to-tap + swipe; /api/screen.jpg, /api/tap,
  /api/swipe; scripts/ota.ps1 + scripts/shot.ps1 for hands-off flash+screenshot.
- JPEG quality/size is capped at a 16 KB boot buffer to stay under the TLS heap floor;
  if a busy screen exceeds it the shot fails (503). Tune quality, or alloc a bigger
  buffer only on PSRAM boards.

## No-PSRAM RAM budget (CYD) — the binding constraint
- TLS needs a ~40 KB contiguous block; NetClient SKIPS an HTTPS fetch when the largest
  free block < 42 KB (avoids the OOM heap-corruption crash) — providers serve stale + retry.
- The screenshot JPEG buffer (16 KB) is now LAZY-allocated on the first screenshot
  request, not at boot — so the largest-free-block floor stays clear of the TLS band
  until/unless a remote screenshot is actually taken (raised it 34 KB -> 57 KB and
  stopped the httpsSkip that made TLE/feeds go stale).
- TLEs retained WATCHLIST-ONLY (full lists ~18 KB of Strings froze TLS). Aircraft cap 24,
  METAR cap 12. On the PSRAM CrowPanel, keep full lists + a sprite double-buffer (board-conditional).
- Boot fires ~12 HTTPS jobs serially — consider staggering to cut heap contention.
- **Web-server-off toggle (Health) to reclaim contiguous heap. — DONE (Jun 2026).**
  WebPortal `start()`/`stop()` (routes register in `begin()`; only `start()` opens the
  listener, so a boot-off device never does a wedging begin->end->begin). It **boots OFF
  by default** (`webOnBoot`, default false) so the feeds get max contiguous heap; the
  Health **Web** toggle (persists `webOnBoot`) and a **serial console** (`web on|off`,
  `heap`, `reboot`) re-enable it — no lockout, and a reboot always comes up off.
  Measured ~65 KB largest free block with the server off vs ~16–30 KB with it + the
  screenshot buffer up. (Confirmed: splitting the settings page from the API would NOT
  help — the cost is the shared AsyncTCP machinery, not the routes/HTML.)
- **Unified per-airport METAR pool (v1 DONE, Jun 2026).** `services/MetarStore` is a
  shared per-ICAO pool (lat/lon/hpa/cloud/wind/temp/cat/obs, bounded + LRU). The METAR
  list + pressure map both UPSERT the stations they parse; the pressure map renders the
  UNION of its scope points and the pool in the box — so a station fetched by one feed
  shows for the others (no more "AWC unavailable but the map has data") and they stay
  consistent + denser near the observer. REMAINING: actually de-duplicate the *fetches*
  (consumers query the pool first, fetch only the gaps) and route raw/TAF through it.
- **Two-phase boot: updater -> viewer (DONE, Jun 2026).** Gated by the `bootUpdater`
  setting (off by default): a lean boot brings up only WiFi/NTP/net + the cacheable
  providers (TLE/Launch/SpaceWx) — no UI, no live feeds, no screenshot buffer — refreshes
  whatever's stale, then reboots into the viewer (cache fresh, RTC keeps the clock valid
  so the providers skip re-fetching). Re-evaluates each boot and can chip across several
  update boots, with a cycle guard so it can't loop. Currently optional since the lazy
  screenshot buffer already keeps the viewer above the TLS floor. Possible refinement:
  one data-type per update boot for the very leanest phase; a scheduled re-enter on TTL.
- LESSON: Settings::backfillDefaults() now adds missing keys on every load, because a
  stale settings.json + the web form's Save-all once blanked focusEnabled/inactivitySec/
  dim*/lead-times to 0/false — do NOT let the form write keys that weren't loaded.

## Remaining feature ideas (not yet built)
- **Aviation TRUE surface fronts** — still BLOCKED on a confirmed data source. The
  makeshift H/L + cloud pressure map from major-airport METARs SHIPPED (Aviation
  "Pressure" sub-view: blue=high/red=low, H/L markers, observer crosshair, cloud-cover
  rings, hPa/inHg, US or worldwide). Real WPC *front polylines* still need a reachable,
  parseable fronts/H-L product (the codsus.php endpoints 404) — don't fabricate coded
  lat/lon/mb positions or add a flaky HTTPS provider to a heap-starved board on a hunch.
- **Aircraft flight trails** — accumulate recent observed positions per hex and draw
  fading tracks (the ADS-B point feed has no history). Adds retained heap → gate on
  no-PSRAM pressure.
- **Offline / no-internet mode — PARTIAL (Jun 2026).** Boot path done: the WiFiManager
  captive portal runs non-blocking and a **screen tap drops to offline field mode**
  (portal splash shows the setup AP + the tap-to-skip hatch); the WiFi-down reboot
  watchdog is suppressed offline; LocationService seeds from the last-known fix so
  satellites/star map/orrery have a location with no network; `/api/status` reports
  `offline`. REMAINING: a runtime offline toggle + "offline" on-screen glyph + marking
  stale-but-usable data; and the **pre-offline refresh** (a foreground TLE/launch/
  space-wx/location update pass on the offline transition, same path as the two-phase
  boot updater) so you go offline with the freshest caches.
- **Rover/space imagery (PSRAM boards only)** — NASA mars-photos latest photo + APOD
  would be amazing on the bedside, but JPEG download + decode + a full framebuffer
  needs PSRAM — gate to CrowPanel. No-PSRAM CYD stays text-only (rover summary feed).
- **Domain-based data release** — when in a heap-hungry domain (Aircraft), release
  String-heavy data from cold domains (TLE/METAR) + drop their poll rate; trigger on
  `heapBlkMin` near the floor; no-PSRAM only.

## Shipped this sweep (Jun 2026) — removed from the lists above
Missions content (Mars distance/light-time + surface map + Earth-facing overlay; Moon
phase/illumination + near & far-side landing-site maps + day/night shading; Deep Space
live mission panel) folded into the Solar System tab. Real Natural Earth coastlines +
country/state borders + Mars/Moon feature maps (tools/gen_worldmap.py). Web settings
revamp, debug-screenshot memory toggle, makeshift METAR pressure/cloud map, rise/set +
transit per body, upcoming meteor-showers page, naked-eye visibility.

## Shipped this sweep (late Jun 2026) — removed from the lists above
- **Clock-mode overlay** (tap the time): static lower-right on live pages, corner-hop on
  calm pages, sprite-rendered, 24h/AM-PM + digital/analog-ball toggles + date. Replaces
  the desk-clock-shell plan (see top section).
- **3×3 grid live tiles** — each surfaces its key live token (next AOS+sat, T-minus+
  mission, nearest METAR, planets/constellations up, Kp, health), word-wrapped.
- **Heap floor fix** — lazy screenshot buffer (34->57 KB largest block); providers now
  report Ready from a fresh persisted cache + restore lastFetched across reboots (fixes
  "TLE ancient"). **Two-phase boot** updater built (gated).
- **Agenda** — tap focuses the exact bird/launch; tonight's visible planets +
  constellations replace the far-off meteor countdown. Aircraft auto-selects + cycles.
  Satellites dropped the redundant pass az/el graph view.
- **Remote** — `/remote` up/down scroll buttons + 2×2 layout; bigger screenshot.

## Web UI overhaul + user-friendly configuration (v1 DONE, Jun 2026)
Tabbed settings app SHIPPED (left-nav: Location / Focus / Satellites / Bodies /
Appearance / Aircraft / System). DONE:
- **Tabbed layout** — all sections in the DOM, CSS-toggled, one Save.
- **Locations tab** — Leaflet map + **address geocode** (Nominatim) + **saved-locations
  list** (add / use / delete, persisted as the `locations` array) + name/lat/lon/source.
- **Focus tab** — per-page **day/night ambient-tour checkboxes** (build ambientDay/Night;
  no typo-prone strings) + lead/threshold fields.
- **Satellites / Bodies** — checkbox pickers + comma-sep extras.
- **Memory Skies tab — DONE (Jun 2026)** — Leaflet map picker + geocode + "My location"
  browser-geolocation, add/use/delete saved `memorySkies` (see M9).
- **Browser geolocation — DONE (Jun 2026)** — "My location" button on the Location +
  Memory-Skies maps (graceful when the browser blocks it on plain http).
REMAINING (stretch):
- **Full sat/body pickers** — search/filter the live TLE catalog + full body list on the
  device (the web UI still uses a baked preset + free-text). Pairs with orrery->LittleFS.
- **Reorder the focus tour** (drag/up-down) — order currently follows the carousel.
- **Per-page "what am I looking at" explanations.**

## Shipped this sweep (mid Jun 2026) — removed from the lists above
- **Personal/memory skies** (M9) + the **Memory Skies** web tab with a Leaflet picker.
- **Star + constellation database** — real generated catalog (HYG + d3-celestial),
  zoom-reveal of fainter stars, ASCII-folded names (Boötes->Bootes). `tools/gen_stars.py`.
- **Status-bar chrome** — WiFi signal bars (tap->Health), mode glyph (play/pause/lock),
  location crosshair -> on-device saved-locations picker (M11).
- **Offline field mode** — tap past the WiFi portal; watchdog-reboot suppressed; last-fix
  location fallback.
- **Aviation** — pressure-map discrete tap-to-zoom levels (off/2.6/4.5/7x); Hazards
  sub-view hidden when empty; extreme-weather (TS/hail/heavy precip/strong wind, now or
  forecast) + hazards surfaced as a Director badge + first-appearance alert.
- **Solar System** — orbit view shows each body's orbital speed (km/h + mph).
- **Health** — provider ages + uptime in d/h/m/s. **Web** — location field layout fix +
  Source auto/preset explanation.

<!-- new milestones append below as they land -->
