# Polish backlog

Deferred polish — cut from the first pass to keep momentum. Pick up later.
(Bugs/blocking work go in commits, not here.) Items shipped are removed; this list
is the *remaining* work as of the latest sweep.

## UX shell — "desk-clock" reorganization (do in a BRANCH; big, don't disrupt main)
The vision: make it the best air-&-space *desk clock*, not a flat 9-tab carousel.
- **"Now" home face** — the resting/default view: big local time + date, sun/moon +
  day-night, and the single most-relevant thing right now (next pass / launch /
  clear-dark window), fed by the Director. Day = clean clock; night = "observatory"
  (the existing dark/red star-map + orrery rotation).
- **3x3 quick-jump grid** — tap the clock/status to open a grid that jumps by domain
  instead of swiping linearly. Group the tabs: Sky tonight (Agenda/Solar System/Star
  Map), Things moving (Satellites/Launches/Aircraft), Conditions (Aviation/Space Wx).
- **Health as a corner-glyph MODAL OVERLAY** (with Settings/Location) — off the
  carousel. (Supersedes the M1/M11 app-shell + Health-overlay notes below.)
- Ties into the memory strategy: keep the active *domain* hot, let other domains
  release String-heavy data (TLE/METAR/aircraft) + drop to low-rate polling on
  no-PSRAM boards, triggered by `heapBlkMin` near the floor.

## M0 — bring-up / HAL
- Verify 4" CYD (ST7796) + CrowPanel panels on real hardware (only 2.8" CYD verified).
- CrowPanel: confirm RGB porch timings, I2C-expander backlight, GT911 coexistence on hw.
- DS3231/PCF8563 RTC driver (currently a stub; NTP-only for now).

## M1 — services / infra
- core/Canvas + Renderer abstraction + reusable widget toolkit (pages still draw via Display::gfx()).
- Full app-shell chrome: 3x3 quick-jump grid, corner-glyph overlays (Settings/Health/Location).
- **Auth on the settings API**: ElegantOTA is basic-auth'd, but `POST /api/settings`,
  `/api/tap`, `/api/swipe`, `/api/screen.jpg` are open on the LAN. Gate them.
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
- Proper Natural Earth coastline bundled in LittleFS (current is a coarse hand outline).
- Grayline / day-night terminator overlay on the ground track (needs Ephem subsolar).
- Doppler: uplink correction + tuning readout; rotor Az-El output.

## M4 — Launches
- More filter chips: by provider/site/country. (Time window 24h/7d/30d/all + hide-TBD done.)
- Detail view on centre-tap (full mission text, window open/close, weather, image).
- RocketLaunch.Live fallback parser: verify pad/location/mission paths on a live 429.
- Streaming JSON parse off the UI thread for the larger detailed mode.

## M5 — Aircraft
- Nearest-airport + likely-frequency (bundle OurAirports subset in LittleFS) — the
  headline ham/SDR feature; needs a generated dataset.
- More filter chips: altitude band, category (airliner/GA/heli/mil), emergency-only.
  (Hide-on-ground + range steps + recenter-on-nearby-airport + tap-on-blip are done.)
- Callsign labels on the radar blips.
- Local-feeder auto-discovery / mDNS; adsb.lol secondary source.
- Flicker: radar still clears its circle bbox each tick — per-blip erase or a PSRAM sprite.

## M6 — Solar System
- **Saturn's moons** (Titan/Rhea/Dione/Tethys) on the moons & rings view — needs its
  own satellite theory (Meeus ch.46) or a calibrated circular model. (Jupiter's
  Galilean moons + Saturn's ring-opening are done + verified vs Horizons.)
- **Live spacecraft** (Psyche probe, Voyager 1/2, Lucy): non-Keplerian / off-scale, so
  a small "missions" text panel (heliocentric distance + one-way light-time) fed by
  JPL Horizons, rather than plotted dots. (Asteroids Ceres/Vesta/16 Psyche + Starman
  are already plotted from baked elements.)
- Periodically refresh the baked Roadster/asteroid elements (drift over years).
- Rise/set + transit times per body (currently instantaneous alt/az only).

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
- Expand the catalog (PC-generated to mag ~5.5, ~1000-1600 stars) bundled in LittleFS;
  current is ~40 brightest in flash. Measure render time on no-PSRAM.
- More constellation lines + labels. (Sun/Moon/planets are now plotted on the chart.)
- Pan/zoom (+/- buttons), magnitude limit persisted; gridlines / ecliptic.

## M10 — Agenda + observability
- Sky Window: moon illumination shading intensity; precip overlay; finer (30-min) buckets.
  (Local-time labels on the +6/+12/+18 ticks done; tapping an event jumps to its tab.)
- Pre-focus the target on jump (pass -> select that bird, not just the Satellites tab).
- Context title Today/Tonight by time of day; meteor-shower peak markers.

## M11 — System/Health
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
- The screenshot JPEG buffer (16 KB) is allocated ONCE at boot (fresh heap); a runtime
  malloc/free of that size fragments the heap and drops the largest block below the TLS
  floor (it starved aircraft/spacewx — that's why it's boot-allocated now).
- TLEs retained WATCHLIST-ONLY (full lists ~18 KB of Strings froze TLS). Aircraft cap 24,
  METAR cap 12. On the PSRAM CrowPanel, keep full lists + a sprite double-buffer (board-conditional).
- Boot fires ~12 HTTPS jobs serially — consider staggering to cut heap contention.
- LESSON: Settings::backfillDefaults() now adds missing keys on every load, because a
  stale settings.json + the web form's Save-all once blanked focusEnabled/inactivitySec/
  dim*/lead-times to 0/false — do NOT let the form write keys that weren't loaded.

## New feature ideas (this session's stream-of-consciousness, not yet built)
- **Web settings UI revamp**: set geolocation on a *map*; pick satellites from a
  checkbox/preset list (not name-search); checkbox the trackable celestial bodies
  (Roadster/Psyche/asteroids); editable mDNS hostname. (task)
- **Aviation surface-fronts map** — BLOCKED on a confirmed data source: the WPC
  "Coded Surface Bulletin" endpoints I tried (codsus.php / codsus_disc.php) 404, and
  I won't guess the coded lat/lon/mb format (would risk fabricated positions) or add
  a flaky HTTPS provider to a heap-starved board on a hunch. Need to first confirm a
  reachable, parseable fronts/H-L product (URL + format). Tier 1 (H/L centre markers
  on the coastline map) then Tier 2 (front polylines, region-filtered + vertex-capped).
  VIABLE PATH (recommended, no new data source): build a makeshift H/L pressure map
  from METAR altimeter/SLP — one aviationweather.gov request for a fixed spread of
  major airports (KLAX, KSFO, KDEN, KORD, KMSP, KDFW, KIAH, KATL, KMIA, KJFK, KBOS,
  KSEA, KMCI, KSLC, ...). Plot each on the coastline map coloured by pressure
  (blue=high, red=low), label the max as H and min as L (makeshift centres), and a
  crude gradient. Same proven feed as AviationWxProvider; small body; no fronts data
  needed. (Nearby-METAR interpolation gives the *local* gradient too.)
- **Solar System "upcoming showers/comets" page** — list all upcoming meteor showers
  (and comets) in date order even if far out, with per-location visibility notes;
  promote to the Agenda when within a few days (the Agenda already shows the next one).
- **Aircraft flight trails** — accumulate recent observed positions per hex and draw
  fading tracks (the ADS-B point feed has no history). Adds retained heap → gate on
  no-PSRAM pressure.
- **Production memory toggle** — gate the 16 KB debug-screenshot buffer behind a
  setting; off frees ~16 KB of contiguous heap (lifts HTTPS over the TLS floor).
- **Missions / Mars-rover page** — kid-inspiring "what's happening out there right
  now" page: Mars facts + sol/season, active rovers (Perseverance/Curiosity) +
  latest status, and live spacecraft (Voyager 1/2, Psyche, Europa Clipper) one-way
  light-time + heliocentric distance. Mostly static facts refreshed occasionally +
  a light status feed; light on heap. Strongly on-mission (bring far missions to the
  bedside). (See also M6 "live spacecraft" panel.)
  - ~~**Moon-missions view**~~ DONE — Missions page 3rd sub-view: live Moon
    phase/illumination/distance + near-side map of real landing sites (Apollo/robotic/
    CLPS) + sub-Earth (libration) marker. Past landings only (real coords); upcoming
    flights named but undated on purpose. Future: pull a live feed for dated upcoming
    launches (Artemis II, CLPS) when one's available.
  - **Rover/space imagery (PSRAM boards only):** NASA mars-photos latest photo + APOD
    would be amazing on the bedside, but JPEG download + decode + a full framebuffer
    needs PSRAM — gate to CrowPanel. No-PSRAM CYD stays text-only (rover summary feed).
- **Domain-based data release** — when in a heap-hungry domain (Aircraft), release
  String-heavy data from cold domains (TLE/METAR) + drop their poll rate; trigger on
  `heapBlkMin` near the floor; no-PSRAM only.

<!-- new milestones append below as they land -->
