# Polish backlog

Deferred polish per milestone — intentionally cut from the first pass to keep
momentum. Pick up later. (Bugs/blocking work go in commits/tasks, not here.)

## M0 — bring-up / HAL
- Verify 4" CYD (ST7796) + CrowPanel panels on real hardware (only 2.8" CYD verified).
- CrowPanel: confirm RGB porch timings, I2C-expander backlight, GT911 coexistence on hw.
- DS3231/PCF8563 RTC driver (currently a stub; NTP-only for now).

## M1 — services / infra
- core/Canvas + Renderer abstraction + reusable widget toolkit (pages still draw via Display::gfx()).
- Full app-shell chrome: 3x3 quick-jump grid, corner-glyph overlays (Settings/Health/Location).
- WebPortal: richer settings UI (forms, not raw JSON), auth on all routes, OTA basic-auth verified.
- Open-Meteo timezone refinement (DST rules; currently fixed offset from IP).
- GPS location source (module optional).

## M2 — astro core
- Enable/validate Ephem (GPLv3) for full planet precision when the Solar System tab needs it.
- deltaT refinement; higher-precision sun/moon if required.
- Turn ASTRO_SELFTEST off in release builds.

## M3 — Satellites
- On-device group filter chips + sunlit-only toggle; AMSAT mode/band sub-filters.
- Source the live AMSAT transponder set (not the hand seed in Transponders.h).
- Proper Natural Earth coastline bundled in LittleFS (current is a coarse hand outline).
- Grayline / day-night terminator overlay on the ground track (needs Ephem subsolar, m6).
- Doppler: uplink correction + tuning readout; rotor/Az-El output.
- Watchlist editing on-device (currently seeded; edit via web only later).

## M4 — Launches
- On-device filter chips: time window (24h/7d/30d), hide-TBD, by provider/site/country.
- Detail view on centre-tap (full mission text, window open/close, weather, image).
- RocketLaunch.Live fallback parser: verify pad/location/mission field paths on a live 429.
- Streaming JSON parse off the UI thread for the (larger) detailed mode.

## M5 — Aircraft
- Nearest-airport + likely-frequency (bundle OurAirports subset in LittleFS) — the
  headline ham/SDR feature, deferred because it needs a generated dataset.
- Filter chips: altitude band, category (airliner/GA/heli/mil), hide-on-ground, emergency-only.
- Tap-on-blip selection (currently tap edges to step); callsign labels on the radar.
- Local-feeder auto-discovery / mDNS; adsb.lol secondary source.
- Flicker: radar still clears its circle bbox each tick — per-blip erase or a PSRAM sprite.

## Cross-cutting — rendering
- Anti-flicker done via _needClear + in-place opaque/padded text + blip-erase. Next:
  optional LGFX sprite double-buffer on PSRAM boards (CrowPanel) for zero-flicker everywhere.
- Page-state widget (loading/empty/error/stale) shared component (spec §13) instead of ad-hoc messages.

## M6 — Solar System
- Grayline / day-night terminator + subsolar point overlay on the Satellites
  ground track (astro::Sun gives subsolar lat=dec, lon from GMST) — spec §6.
- Rise/set + transit times per body (currently instantaneous alt/az only).
- Moon phase ICON (drawn crescent/gibbous) rather than just the phase name.
- Enable VSOP87 Ephem (ENABLE_EPHEM) for arcsecond precision when wanted;
  current Schlyter is ~arcmin (outer planets worse, no planet perturbations).
- Persist the show-filter (all/up/eye) in Settings.

## M7 — Intelligent Focus + theming
- Notice/banner before an auto-switch (silent | banner-then-switch | banner-only)
  + buzzer chirp / CW announce (spec §7.5) — not yet (no banner widget/buzzer).
- Master AUTO/MANUAL toggle as a status-strip glyph (currently only the
  interaction->MANUAL->inactivity->AUTO lifecycle + long-press pin).
- Scoring with urgency/viewability bonuses + hysteresis margin + anti-flap dwell/
  cooldown (current Director uses simple priority: pass > launch > ambient).
- Visible-ISS boost + cloud-gated viewability (needs WeatherProvider, m10).
- Geomagnetic (Kp) banner trigger (needs SpaceWx, m8); aircraft-emergency trigger.
- Pulsing/lead-time tab badges (currently a steady warn dot).
- Red dark-adapt palette is wired (nightPalette=red) but default dark; tune palettes.

## M8 — Space Weather
- A/K indices + sunspot number; short Kp history sparkline (NOAA or hamqsl XML).
- hamqsl.com band-condition XML as a richer alternative to the local heuristic.
- Aurora visibility estimate from Kp + observer latitude.
- Kp trigger currently only badges Space Wx; optional banner/auto-switch (m7 polish).

## No-PSRAM RAM budget (CYD)
- TLEs are retained WATCHLIST-ONLY (full amateur+stations ~18KB of Strings froze
  TLS). On the PSRAM CrowPanel we could keep the full list (board-conditional).
- NetClient skips an HTTPS fetch when the largest free block < 45KB (avoids the
  OOM heap-corruption crash); providers then serve stale + retry. Boot fires ~12
  HTTPS jobs serially — consider staggering/chaining to cut heap contention.
- Aircraft cap 24, METAR cap 12, to bound resident heap.
- Consider an LGFX sprite double-buffer ONLY on the CrowPanel (PSRAM) — not the CYD.

## Aviation weather tab  (Phase 1 METAR/TAF + Phase 2 sounding + 2b hazards shipped)
- Phase 2 (done): Op40 sounding (temp/dewpoint profile + freezing level + winds
  aloft); Phase 2b (done): AIRMET/SIGMET + PIREP hazards view + SPECI Director badge.
- Decode TAF periods; Skew-T proper skew + isotherms; soaring indices (Thermal
  Index, trigger temp, top of lift); decode AIRMET/SIGMET into plain phrases.
Goal: a real "flight weather brief", all from the keyless NOAA Aviation Weather
Center API (aviationweather.gov/api/data/...) + rucsoundings.
- Phase 1 (done): nearby METARs via bbox, decoded card (wind/vis/ceiling/temp/
  dewpoint/altimeter/flight category) + raw METAR + raw TAF, tap to step stations.
- Decoded TAF (forecast periods, not just raw); home-field pin / favourite.
- Skew-T / atmospheric SOUNDING: temp+dewpoint vs altitude, lapse rate, inversion
  height, lifted index, wind barbs, convective/soaring index.
  !! DATA SOURCE DEAD: rucsoundings.noaa.gov is decommissioned (ECONNREFUSED on
     80 and 443, confirmed off-device 2026-06). SoundingProvider currently fails ->
     "sounding unavailable". REPLACE with Open-Meteo pressure-level fields
     (temperature_1000hPa..10hPa, dewpoint_*hPa, windspeed/winddirection_*hPa) — the
     same keyless HTTPS api.open-meteo.com we already use in WeatherProvider; map each
     hPa level to altitude (geopotential_height_*hPa) and reuse the existing GSD
     parse's freezing-level/level model. Clean swap, no new dependency.
- Winds & temps aloft (FB) -> FREEZING LEVEL (interp temp=0) + wind/temp by altitude.
- AIRMET/SIGMET + G-AIRMET hazards (icing, turbulence, IFR, mtn obscuration, FZLVL)
  near the observer; PIREPs (turbulence/icing/cloud tops); CWAs.
- Pressure: SLP + rising/falling trend from the METAR (synoptic fronts/highs-lows are
  graphical WPC products -> out of scope on this screen).
- Director: badge/notice on a new METAR or a SPECI (off-cycle special report), and
  on a new sounding / hazard for the home field (spec §7-style trigger).
- Flight-category color is in the decoded card; add a nearby-field category strip.

## Future tab ideas (in SPEC §14)
- Moon/Sun detail + EVENTS: moonrise/set + transit; and special events not yet
  covered anywhere — lunar/solar eclipses, blood moon (total lunar), supermoon,
  equinox/solstice. Pragmatic: a small static table of upcoming eclipse/supermoon
  dates (like the meteor-shower table) surfaced in Agenda + a Sun/Moon tab, with
  the Director flagging an imminent eclipse. (Moon PHASE + illumination already
  done: Solar System tab + astro::SolarSystem + Agenda moon-up shading.)
- Sun/Moon rise/set + transit (peak/culmination) as DIRECTOR inputs: golden-hour
  cue at sunset, sun/moon transit ("peak") markers. (DONE: sunrise/sunset/moonrise/
  moonset now surface as Agenda events + Sky Window markers, from the hourly sun-alt
  / moon-up crossings. Remaining: transit/peak markers and wiring these into the
  Director as focus inputs.)
- Aviation weather: METAR/TAF + atmospheric soundings (Skew-T, lapse rate,
  inversion, lifted index) for glider/soaring analysis. NOAA AWC (no key) +
  RAOB/RAP soundings. Director triggers: new METAR/TAF, SPECI, new sounding.
  Reuses the OurAirports subset; pairs with the Aircraft tab.

## M10 — Agenda + observability
- Sky Window: moon illumination shading intensity; precip overlay; finer (30-min) buckets.
- Director cloud gating: reduce visible-pass viewability bonus when overcast; observing-
  window banner ("clearing 22:00-00:30") (spec §7.2) — provider exists, Director not wired.
- Tap a Sky Window event to jump to its tab pre-focused (spec §6 Agenda).
- Context title Today/Tonight by time of day; meteor-shower peak markers.

## M11 — System/Health
- Make Health a corner-glyph MODAL OVERLAY (with Settings + Location) instead of a
  carousel tab, once the overlay chrome exists (spec §4.1).
- Per-provider "next poll" column + last HTTP status code (need providers to expose them).
- Confirm dialog before Reboot; toast after Refresh; on-device settings editing.

## M9 — Star Map
- Expand the catalog (PC-generated to mag ~5.5, ~1000-1600 stars) bundled in
  LittleFS; current is ~40 brightest in flash. Measure render time on no-PSRAM.
- More constellation lines + labels; planets/Moon/Sun plotted on the chart.
- Pan/zoom (+/- buttons), magnitude limit persisted; gridlines / ecliptic.

## M12 — Polish
- WebPortal settings form covers §7.10; add watchlist editing (array) + presets
  add/edit with Open-Meteo geocoding (city -> lat/lon), per spec §6 Location.
- Auth on the settings page + API (ElegantOTA auth is set; settings POST is open).
- Settings schema versioning migrate() only fills a few keys — extend as schema grows.
- Final theming: tune day/red palettes; per-widget contrast pass on real panels.

<!-- new milestones append below as they land -->
