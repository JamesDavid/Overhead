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

<!-- new milestones append below as they land -->
