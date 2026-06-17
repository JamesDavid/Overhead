# CYD_NOTES.md — ESP32 "Cheap Yellow Display" quirks (hard-won)

Quirks and gotchas for the **ESP32-2432S028R** ("CYD", 2.8" ILI9341, resistive
touch, **no PSRAM**) as used by this project (`cyd28_ili9341`). Most of these cost
real debugging time — read before changing display, screenshot, OTA, or WiFi code.
See also `PIO_DEBUG.md` (the autonomous flash/screenshot/control loop) and the
no-PSRAM budget section there.

---

## Display (ILI9341, LovyanGFX)

- **Rotation 6** is what un-mirrors *this* unit (the panel is wired with a
  horizontal flip; `MADCTL` MV=0 + the BGR bit). If text/graphics come out
  mirrored, that's the rotation, not your drawing.
- 320×240 landscape. `kStatusH = 20` px status strip at top; pages draw below
  (`contentY()=20`). Touch handed to pages is **content-relative** (`screen_y −
  contentY`).
- **Backlight is on GPIO21 and must be driven with the Arduino LEDC API directly**
  (`ledcSetup`/`ledcAttachPin`/`ledcWrite`, channel 7). LovyanGFX's `Light_PWM`
  did **not** drive the backlight on this unit — the screen stayed stuck at one
  brightness. Don't go back to Light_PWM.

## Touch (resistive, XPT2046)

- Resistive, single-touch, needs calibration (stored in settings). The CYD28
  touch needed an axis fix; a stuck/!inverted axis usually means swapped X/Y or a
  sign flip in the calibration mapping.
- The CH340 serial port and the touch SPI can both be live; a held serial monitor
  (PermissionError on COM5) doesn't block touch.

## JPEG screenshot — the colour quirk (this one is subtle!)

The `/api/screen.jpg` debug endpoint reads the panel back and JPEG-encodes it
(bitbank2 **JPEGENC**) on the UI thread (`Display::serviceShot`). The pixel
mapping is **panel-specific and was derived empirically by sampling known theme
colours** — expect to re-derive it on a different panel:

1. The 16-bit value read back from the panel must be **byte-swapped**:
   `c = (c >> 8) | (c << 8)`.
2. After the swap the channels are **NOT** the usual RGB565 order — they decode as:
   - **hi 5 bits = Blue**  → `((c >> 11) & 0x1f) << 3`
   - **mid 6 bits = Red**  → `((c >> 5)  & 0x3f) << 2`
   - **lo 5 bits = Green** → `( c        & 0x1f) << 3`
3. JPEGENC then wants the RGB888 bytes written in **B, G, R** order.

Symptoms if this is wrong: blue/orange swapped, or "crazy/false" colours, or
pixelated garbage. If your first screenshot looks wrong, this mapping is why.

Other screenshot constraints (from the no-PSRAM budget):
- The JPEG buffer is **16 KB, malloc'd once at boot** and never freed. A runtime
  malloc/free of that size fragments the heap below the TLS floor and starves the
  HTTPS providers — so it's boot-allocated on the fresh heap.
- Dense screens overflow 16 KB at medium quality → `serviceShot` retries at low
  quality; if it still overflows the endpoint returns 503.
- Encoding runs on the **UI/loop task**, never the AsyncTCP task, to avoid racing
  the live draw and to keep the web task free for the transfer.

## No-PSRAM RAM budget (the binding constraint)

- **TLS needs a ~40 KB contiguous block.** `NetClient` skips an HTTPS fetch when
  the largest free block < 42 KB (avoids an OOM heap-corruption crash); providers
  serve stale and retry. Any big transient allocation can knock providers offline.
- Data is trimmed to fit: satellite TLEs are **watchlist-only**, aircraft capped
  at 24, METARs at 12. (Lift these only on the PSRAM CrowPanel, board-conditional.)
- No room for a full-screen LGFX sprite double-buffer (320×224×2 ≈ 143 KB) — hence
  flicker is fought with `startWrite()`/`endWrite()` batching + redraw-on-change,
  not double buffering.

## OTA (ElegantOTA v3 over WiFi)

- The single **AsyncTCP** task is the bottleneck. When it's busy (a `/remote`
  browser open, a screenshot mid-transfer, or boot-time HTTPS fan-out) the OTA
  upload returns **`upload=100`** then **`400`** on repeated tries. Recovery:
  let the device **boot-settle ~20 s**, or **two-tap Reboot** on the Health page
  (or serial reset, below), then retry — it returns `upload=200`.
- Boot fires ~12 HTTPS jobs serially; flashing during that window is what most
  often flakes. Wait for uptime > ~20 s.

## WiFi drops + watchdog

- The radio occasionally **drops and does not auto-recover**, which kills OTA and
  the debug API (status times out, `upload=000`). A WiFi watchdog in `loop()` now
  nudges `WiFi.reconnect()` after ~8 s down and **reboots after ~90 s** down;
  `WiFi.setAutoReconnect(true)` is also set.
- **Manual recovery when it's wedged** (no WiFi, `upload=000`): hard-reset over USB
  serial — esptool asserts DTR/RTS to reset, `--after hard_reset` boots the app:
  ```
  ~/.platformio/penv/Scripts/python.exe -m esptool --port COM5 --before default_reset --after hard_reset flash_id
  ```
  CH340 is on **COM5**. Wait ~18 s for boot + WiFi rejoin, then OTA-flash normally.

## Running smoothly on a limited device — efficiency findings

The binding resource is **contiguous heap** (the ~42 KB TLS floor), not CPU or the
number of requests. Findings + what's implemented:

- **HTTP requests already use one serialized queue.** `NetClient` runs a single
  core-0 `NetTask` that drains one request queue — only **one TLS session exists at
  a time**, never concurrent. So "use a single queue" is already done; adding more
  parallelism would be *worse* (concurrent TLS = multiple 40 KB peaks). Keep it serial.
- **The TLS floor can't be lowered on this platform.** mbedtls' 16 KB RX/TX content
  buffers are baked into the **precompiled** arduino-esp32 (classic `espressif32`)
  libs; `WiFiClientSecure::setBufferSizes()` doesn't exist in 2.0.x. Shrinking them
  needs a source/sdkconfig build (`CONFIG_MBEDTLS_SSL_IN/OUT_CONTENT_LEN`) — i.e. the
  pioarduino/IDF path, not available here. So ~40 KB contiguous is the hard TLS cost.
- **The 16 KB debug screenshot buffer is a major headroom consumer.** It's boot-
  allocated (to avoid fragmentation) and permanently occupies a contiguous 16 KB —
  right out of the band TLS needs. For *production* (no remote debugging) it would
  free ~16 KB of headroom to gate it behind a setting. We keep it on while developing.
- **Retained data pins the floor → starvation loop.** Holding String-heavy data
  (e.g. 24 ADS-B contacts) under the floor makes `NetClient` skip the very fetch
  meant to refresh it, so everything goes stale for many minutes. Fix pattern:
  **release stale data to free heap** — `AircraftProvider::poll()` now clears (and
  `shrink_to_fit`s) a contact set older than 60 s.
- **Monitor memory pressure**, exposed in `/api/status`:
  - `heapBlk` — current largest free block; `heapBlkMin` — **low-water** (the key
    number: if it sits below ~42 K, HTTPS is being starved).
  - `httpsSkip` — count of fetches skipped at the floor (climbing = chronic pressure).
  - Existing: `heap` (free), `netInFlight`, per-provider status, `[loop] dt` serial log.
- **Other levers** (cheap, not yet all applied): longer provider TTLs to fetch less
  often; `reserve()` long-lived vectors to cut fragmentation; prefer fixed `char[]`
  over `String` for hot/transient fields; stagger the ~12 boot fetches; cap retained
  lists tight (TLE watchlist-only, aircraft 24, METAR 12). A heap-floor reboot in the
  watchdog is a last resort (risks a reboot loop if pressure is chronic).

## Settings

- `Settings::backfillDefaults()` re-adds any missing keys on every load. This
  exists because a stale `settings.json` + the web form's "save all" once blanked
  keys to 0/false (broke alerts/backlight/timeouts). Never let a form write keys it
  didn't load.

## Toolchain

- PlatformIO isn't on PATH: `~/.platformio/penv/Scripts/platformio.exe run -e cyd28_ili9341`.
- The S3 CrowPanel env uses the **pioarduino** platform (arduino-esp32 3.x); its
  install glitch needs `python -m pip install esptool==5.3.0` once. The classic
  `cyd28`/`cyd4` envs are espressif32 (arduino 2.0.x).
