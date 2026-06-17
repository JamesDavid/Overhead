# Polish backlog

Deferred polish — cut from the first pass to keep momentum. Pick up later.
(Bugs/blocking work go in commits, not here.) Items shipped are removed; this list
is the *remaining* work as of the latest sweep.

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
- On-device filter chips: time window (24h/7d/30d), hide-TBD, by provider/site/country.
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
  (current Director is simple priority: pass > launch > ambient tour).
- Cloud-gated viewability: reduce a visual pass's "worth it" when overcast; observing-
  window banner ("clearing 22:00-00:30") — WeatherProvider exists, not wired in.
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
- Decode AIRMET/SIGMET into plain phrases (currently raw text).
- Home-field pin / favourite; nearby-field flight-category strip.
- Pressure trend (SLP rising/falling) from the METAR.
- Proper Skew-T skew + isotherms; lifted index; wind barbs (vs the text winds).
- Optional: decode TAF cloud layers as a forecast ceiling timeline (Open-Meteo stays the
  primary cloud source for the Agenda Sky Window — TAF is airport-only + coded).

## M9 — Star Map
- Expand the catalog (PC-generated to mag ~5.5, ~1000-1600 stars) bundled in LittleFS;
  current is ~40 brightest in flash. Measure render time on no-PSRAM.
- More constellation lines + labels. (Sun/Moon/planets are now plotted on the chart.)
- Pan/zoom (+/- buttons), magnitude limit persisted; gridlines / ecliptic.

## M10 — Agenda + observability
- Sky Window: moon illumination shading intensity; precip overlay; finer (30-min) buckets.
- Tap a Sky Window event to jump to its tab pre-focused.
- Context title Today/Tonight by time of day; meteor-shower peak markers.

## M11 — System/Health
- Make Health a corner-glyph MODAL OVERLAY (with Settings + Location) once overlay chrome exists.
- Per-provider "next poll" + last HTTP status (need providers to expose them; /api/status
  now reports adsb/tle/avwx/kp/sfi for remote diagnosis).
- Confirm dialog before Reboot; toast after Refresh.

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

<!-- new milestones append below as they land -->
