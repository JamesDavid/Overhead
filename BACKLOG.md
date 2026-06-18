# Polish backlog

Deferred polish — cut from the first pass to keep momentum. Pick up later.
(Bugs/blocking work go in commits, not here.) Items shipped are removed; this list
is the *remaining* work as of the latest sweep.

## UX shell — "desk-clock" reorganization (do in a BRANCH; big, don't disrupt main)
The vision: make it the best air-&-space *desk clock*, not a flat 9-tab carousel.
Additive — the existing pages + Director logic stay; this adds a home page + an
overlay nav layer on top.

### "Now" home face (new default/resting page = `PageHome`)
Big clock + the single most-relevant thing right now, fed by the Director. The device
returns here when idle (instead of the ambient tab tour).

```
+------------------------------------------+
| @6:14^ 19:42v   ( 19% waning   Wed Jun 18|  sun/moon + date strip
|                                          |
|              21:47                        |  BIG time (center, ~64px)
|                                          |
|  > NEXT  ISS pass 22:14  (in 27m)        |  Director "right now" card
|          max 61 deg . SW->NE . 6 min     |
|  o clear & dark until 00:30              |  secondary line (observing window)
|                              [s] [o] [h] |  corner glyphs: settings/loc/health
+------------------------------------------+
            tap center -> grid
```
- **Day** = clean clock (dark or a light palette); **night** = "observatory": red
  dark-adapt palette + a faint rotating starfield/orrery behind the clock (reuse the
  Star Map / orrery renderers), dimmed backlight.
- The NEXT card = whatever the Director scores highest (pass / launch T-minus /
  clear-dark window / aircraft emergency) — same scoring that drives auto-switch
  today, surfaced as a card instead of a tab jump.

### 3x3 quick-jump grid (tap the clock -> `GridOverlay` modal)
Jump by domain instead of swiping linearly. Rows grouped by theme; each cell shows a
live micro-status + the Director attention dot.

```
+-------------+-------------+-------------+
|   Agenda    | Solar System|  Star Map   |  -- SKY TONIGHT
|  tonight    |  orbits     |  1600 stars |
+-------------+-------------+-------------+
| Satellites .|  Launches   |  Aircraft ! |  -- THINGS MOVING
|  ISS 27m    |  T-2d       |  8 near     |
+-------------+-------------+-------------+
|  Aviation   |  Space Wx   |   Health    |  -- CONDITIONS / SYS
|  VFR        |  Kp3        |  ok         |
+-------------+-------------+-------------+
   . = pass imminent     ! = emergency squawk
```
- Each cell = icon + label + one live token (next-pass countdown, T-minus, contact
  count, flight category, Kp...). Tap a cell -> that tab; back/outside -> clock.
- Attention dot pulses when a domain wants you (pass lead-time, SPECI, emergency) -
  same triggers as today's tab badges.
- 8 content tabs + Health = the 9 cells (Missions already folded into Solar System).

### Ambient / rest behavior (no user input)
The home face becomes the rest state, so the Director no longer tours tabs by default
(a bedside clock shouldn't strobe all night). New `restMode` setting:
- `clock` (default day) — sit on the home face; the NEXT card refreshes as the Director
  re-scores. Calm + glanceable.
- `observatory` (default night) — home-face *background* becomes the live rotating
  star-map/orrery (reuse the existing sky/orrery tour); the inspire-a-kid mode.
- `tour` — current behavior: cycle every content tab on `tourDwellSec` (kiosk).
Director **interrupts fire in all modes**: imminent pass / launch T-0 / aircraft
emergency / SPECI still auto-switch to that tab + banner, then fall back to the rest
mode when the event passes. `focusEnabled` / `tourDwellSec` carry over.

### Clock page — incremental first step (do this before the full reorg)
Lower-risk way in: a separate `PageClock` reached by **tapping the time** in the status
bar, with the rest modes (clock / observatory) *internal* to that page (centre-tap
cycles them). No change to current behavior - purely an added page.
- **Auto-pin on enter, unpin on exit** (reuse the existing pin): park on it and the
  Director won't tour away; swipe off and normal behavior resumes. Keep it OUT of the
  ambient tour rotation so AUTO never lands there on its own.
- **Don't let the pin silently swallow interrupts.** Options (pick one):
  (a) surface the Director's alert ON the clock face (banner/card) and let a tap jump
      to that tab - user stays in control, never yanked;
  (b) a special clock-only pin that shows the alert but doesn't auto-switch-then-bounce
      back to the clock (the jarring there-and-back).
  Lean (a): clock face shows "ISS pass in 4m -> tap" and a tap navigates (which also
  unpins). Best of both - calm by default, one tap to act, no involuntary bounce.
- If it feels good on the device, promote it to the *default* home face later.

### Pieces
- **`PageHome`** default page + night-observatory background; Director returns here.
- **`GridOverlay`** modal opened by centre-tap on Home (and/or a status-bar tap).
- **Health / Settings / Location -> corner-glyph overlays** (off the carousel).
- Existing pages stay as grid destinations; swipe optional/retired.
- Ties into the memory strategy: keep the active *domain* hot, let cold domains release
  String-heavy data (TLE/METAR/aircraft) + drop to low-rate polling on no-PSRAM boards,
  triggered by `heapBlkMin` near the floor.
- Supersedes the M1/M11 app-shell + Health-overlay notes below.

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
- Doppler: uplink correction + tuning readout; rotor Az-El output.

## M4 — Launches
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
- Periodically refresh the baked Roadster/asteroid elements (drift over years).
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

<!-- new milestones append below as they land -->
