// Overhead — Milestone 1 (services + infra).
//
// Boots the full base platform and proves the data pipeline end to end:
//   power -> WiFiManager provisioning -> NTP -> IP geolocation (background fetch
//   on the NetTask) -> cache -> EventBus -> page redraw, plus a LAN settings
//   page and ElegantOTA. The Diagnostics page shows it all live.
//
// Cold-start sequence follows spec §13: WiFi -> time -> location -> first fetch.

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "config.h"
#include "hal/Board.h"
#include "hal/Display.h"
#include "hal/Touch.h"
#include "hal/Rtc.h"
#include "core/EventBus.h"
#include "core/Scheduler.h"
#include "core/Theme.h"
#include "core/ThemeController.h"
#include "core/App.h"
#include "core/Director.h"
#include "core/MorseBeeper.h"
#include "services/Settings.h"
#include "services/Cache.h"
#include "services/NetClient.h"
#include "services/TimeService.h"
#include "services/LocationService.h"
#include "services/Provisioning.h"
#include "services/WebPortal.h"
#include "services/AirportDB.h"
#include "core/ClockOverlay.h"
#include "providers/TleProvider.h"
#include "providers/LaunchProvider.h"
#include "providers/AircraftProvider.h"
#include "providers/SpaceWxProvider.h"
#include "providers/WeatherProvider.h"
#include "providers/AviationWxProvider.h"
#include "providers/SoundingProvider.h"
#include "providers/HazardProvider.h"
#include "pages/PageHealth.h"
#include "pages/PageSatellites.h"
#include "pages/PageLaunches.h"
#include "pages/PageAircraft.h"
#include "pages/PageSolarSystem.h"
#include "pages/PageSpaceWx.h"
#include "pages/PageAgenda.h"
#include "pages/PageStarMap.h"
#include "providers/MarsProvider.h"
#include "providers/PressureMapProvider.h"
#include "pages/PageAviation.h"
#if ASTRO_SELFTEST
#include "astro/SelfTest.h"

#if defined(BOARD_HAS_PSRAM)
#include <esp_heap_caps.h>
#include "mbedtls/platform.h"
// Arduino-ESP32 defaults to CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC, so mbedtls allocates
// its (large, contiguous) TLS handshake buffers from INTERNAL RAM even on an S3 with
// 8 MB PSRAM free. A fragmented internal heap then fails the handshake with -32512
// (MBEDTLS_ERR_SSL_ALLOC_FAILED). Redirect mbedtls to PSRAM so TLS stops starving.
static void* mbedtlsPsramCalloc(size_t n, size_t sz) {
  return heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM);
}
#endif
#endif

// --- HAL ---
static Display display;
static Touch   touch;
static Rtc     rtc;
// --- core ---
static EventBus  bus;
static Scheduler sched;
static App       app(display, touch, bus, sched);
static ThemeController themeCtl;
static MorseBeeper morse;
static Director  director;
// --- services ---
static Settings        settings;
static AirportDB       airportDb;
static Cache           cache;
static NetClient       net;
static TimeService     timeSvc;
static LocationService locSvc;
static Provisioning    prov;
static WebPortal       web;
static TleProvider     tleProv;
static LaunchProvider  launchProv;
static AircraftProvider aircraftProv;
static SpaceWxProvider spaceWxProv;
static WeatherProvider weatherProv;
static AviationWxProvider avwxProv;
static SoundingProvider sndProv;
static HazardProvider   hazProv;
static MarsProvider     marsProv;
static PressureMapProvider pmapProv;
// --- pages ---
static String          gHostname;
static bool            gOffline = false;   // booted past the WiFi portal into offline (field) mode
static PageAgenda*     agendaPage = nullptr;
static PageLaunches*   launchesPage = nullptr;
static PageAircraft*   aircraftPage = nullptr;
static PageAviation*   aviationPage = nullptr;
static PageSatellites* satsPage = nullptr;
static PageSolarSystem* solarPage = nullptr;
static PageStarMap*    starPage = nullptr;
static PageSpaceWx*    spaceWxPage = nullptr;
static PageHealth*     healthPage = nullptr;

static String chipSuffix() {
  uint64_t mac = ESP.getEfuseMac();
  char s[7];
  snprintf(s, sizeof(s), "%02X%02X%02X",
           (uint8_t)(mac >> 16), (uint8_t)(mac >> 8), (uint8_t)mac);
  return String(s);
}

static void splash(const char* msg) {
  auto& g = display.gfx();
  g.fillScreen(gTheme.bg);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.setTextSize(2);
  g.drawString("Overhead", g.width() / 2, g.height() / 2 - 14);
  g.setTextSize(1);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString(msg, g.width() / 2, g.height() / 2 + 12);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString("github.com/JamesDavid/Overhead", g.width() / 2, g.height() - 22);
  g.drawString("DE KE7AQA", g.width() / 2, g.height() - 10);
  display.flushFramebuffer();   // push the off-screen canvas now (loop isn't running yet)
}

// Shown while the WiFi captive portal is open: how to set up WiFi, plus the
// field-mode escape hatch (tap the screen to run offline on cached data).
static void splashPortal(const String& ap) {
  auto& g = display.gfx();
  g.fillScreen(gTheme.bg);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(gTheme.warn, gTheme.bg);
  g.setTextSize(2);
  g.drawString("No WiFi", g.width() / 2, 26);
  g.setTextSize(1);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.drawString("To set up, join this WiFi:", g.width() / 2, 64);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString(ap, g.width() / 2, 80);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString("then open  192.168.4.1", g.width() / 2, 96);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.setTextSize(2);
  g.drawString("- or -", g.width() / 2, 134);
  g.setTextColor(gTheme.ok, gTheme.bg);
  g.drawString("TAP SCREEN", g.width() / 2, g.height() - 52);
  g.setTextSize(1);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString("to use OFFLINE (cached data)", g.width() / 2, g.height() - 30);
  display.flushFramebuffer();   // push the off-screen canvas now (loop isn't running yet)
}

// Two-phase boot (spec backlog): on a no-PSRAM board the big cacheable HTTPS
// fetches (TLEs, launches, space-wx) compete with the live-feed TLS handshakes
// for the ~42 KB contiguous-heap floor. When `bootUpdater` is enabled, do those
// fetches in a LEAN phase right after boot — only the cacheable providers are up,
// no UI, no live feeds, no screenshot buffer — then reboot into the viewer where
// the caches are fresh
// (the RTC keeps the clock valid, so the providers skip re-fetching within TTL)
// and the heap is free for ADS-B / weather TLS.
//
// Reuses the providers' OWN staleness decision: their begin() kicks a fetch only
// for stale groups, so net.inFlight()>0 here means "something still needs
// updating". Each boot: if anything is stale, refresh it in this lean phase and
// reboot; if it's STILL stale next boot (a fetch failed), do another update boot
// — up to kMaxUpdateBoots, so it can chip away across several boots but can never
// loop forever. When everything is fresh, fall through to the full viewer.
// Disabled by default (no behaviour change unless the setting is on).
static void runBootUpdater() {
  if (!settings.getBool("bootUpdater", false)) return;
  long cycles = settings.getInt("bootCycles", 0);
  static const long kMaxUpdateBoots = 5;

  // Warm the LOCATION-dependent WEATHER cache too (Open-Meteo is hourly-good, a good
  // fit for the boot cache). The global feeds (TLE/launch/space-wx) were begun above;
  // weather needs a location, so resolve it first (preset / last-known fix is instant;
  // "auto" kicks IP geoloc, briefly waited for), then begin it so a stale fetch joins
  // this lean pass and is cached before the viewer. (Begun again in the viewer phase —
  // a no-op on a fresh cache.) METARs are deliberately NOT warmed here: their ~12-min
  // SPECI-catching TTL is shorter than a boot cycle, so they'd be "stale" almost every
  // boot and churn the updater — they fetch in the viewer (the cache still displays).
  locSvc.begin(&settings, &net, &bus, &timeSvc);
  for (uint32_t t = millis(); !locSvc.active().valid && millis() - t < 6000; ) { net.poll(); delay(20); }
  if (locSvc.active().valid) weatherProv.begin(&settings, &net, &cache, &bus, &locSvc);

  delay(60);                                             // let the NetTask register any queued jobs
  if (net.inFlight() == 0) {                             // nothing stale -> viewer
    if (cycles) { settings.set("bootCycles", (long)0); settings.save(); }
    Serial.println("[boot] cached data fresh -> viewer mode");
    return;
  }
  if (cycles >= kMaxUpdateBoots) {                       // guard: give up, run the viewer anyway
    settings.set("bootCycles", (long)0); settings.save();
    Serial.printf("[boot] updater guard (%ld boots) -> viewer with stale data\n", cycles);
    return;
  }
  splash("Updating data...");
  settings.set("bootCycles", (long)(cycles + 1)); settings.save();
  Serial.printf("[boot] updater boot %ld: %u fetch(es) in flight, waiting...\n",
                cycles + 1, (unsigned)net.inFlight());
  uint32_t t0 = millis();
  while (net.inFlight() > 0 && millis() - t0 < 30000) { net.poll(); web.loop(); delay(20); }
  delay(500);                                            // flush cache writes to LittleFS
  Serial.println("[boot] update pass done -> reboot");
  delay(50);
  ESP.restart();
}

static uint32_t gHeapBlkMin = 0xFFFFFFFF;   // low-water of the largest free block

static void fillStatusJson(JsonDocument& d) {
  d["fw"]       = OVERHEAD_FW_VERSION;
  d["board"]    = BOARD_NAME;
  d["uptimeS"]  = (uint32_t)(millis() / 1000);
  d["heap"]     = Display::freeHeap();
  d["heapBlk"]  = Display::largestFreeBlock();
  d["heapBlkMin"] = gHeapBlkMin;                 // worst-case contiguous block seen
  d["httpsSkip"]  = (uint32_t)net.httpsSkips();  // fetches skipped under the TLS floor
  d["psram"]    = Display::psramSize();
  d["wifi"]     = WiFi.status() == WL_CONNECTED;
  d["offline"]  = gOffline;                      // user/timeout dropped to offline field mode at boot
  d["ssid"]     = WiFi.SSID();
  d["rssi"]     = WiFi.RSSI();
  d["ip"]       = WiFi.localIP().toString();
  d["timeSync"] = timeSvc.synced();
  d["epoch"]    = (uint32_t)timeSvc.nowUtc();
  const auto& l = locSvc.active();
  d["locName"]  = l.name;
  d["lat"]      = l.lat;
  d["lon"]      = l.lon;
  d["tzOffset"] = l.tzOffsetSec;
  d["netInFlight"] = (uint32_t)net.inFlight();
  d["mode"]     = app.mode() == App::Mode::Auto ? "auto" : "manual";
  d["pinned"]   = app.pinned();
  d["page"]     = app.activeIndex();
  // provider health (0 loading,1 ready,2 stale,3 error)
  d["adsb"]     = (int)aircraftProv.status();
  d["adsbN"]    = (int)aircraftProv.aircraft().size();
  d["tle"]      = (int)tleProv.status();
  d["launch"]   = (int)launchProv.status();           // launch diag: status/count + last HTTP codes
  d["launchN"]  = launchProv.count();
  d["lLL2"]     = launchProv.lastLL2Code();            // primary (thespacedevs); 429 = rate-limited
  d["lRLL"]     = launchProv.lastRLLCode();            // fallback (rocketlaunch.live); 0 = not reached
  d["lFb"]      = launchProv.usingFallback();
  d["avwxN"]    = (int)avwxProv.stations().size();
  d["pmap"]     = (int)pmapProv.points().size();   // pressure-map stations loaded
  d["marsSol"]  = marsProv.perseverance().maxSol;  // -1 if rover feed not loaded
  d["kp"]       = spaceWxProv.kp();
  d["sfi"]      = spaceWxProv.sfi();
}

void setup() {
  Serial.begin(OVERHEAD_LOG_BAUD);
  Serial.setTimeout(50);   // readStringUntil default is 1000ms — a newline-less char on RX froze the UI loop 1s/read
  delay(200);
  Serial.println("\n\n=== Overhead — Milestone 1 (services + infra) ===");
  Serial.printf("board: %s  fw: %s\n", BOARD_NAME, OVERHEAD_FW_VERSION);

  if (!LittleFS.begin(true)) Serial.println("[fs] LittleFS mount FAILED");
  Serial.printf("[apt] airport DB %s\n", airportDb.begin() ? "loaded" : "missing (upload /airports.bin)");
  settings.begin();
  cache.begin();

  display.setRgbOrderOverride(settings.doc()["dispBgr"].is<bool>() ? (settings.getBool("dispBgr") ? 1 : 0) : -1);  // CYD variant R/B swap (applied at init)
  if (!display.begin(settings.getBool("debugShots", true))) Serial.println("[display] init FAILED");
  display.applyDisplayPrefs((int)settings.getInt("dispRotation", DISPLAY_DEFAULT_ROTATION), settings.getBool("dispInvert", false));
  touch.begin(display);

#if ASTRO_SELFTEST
  // Validate the astro core before the network (which may block on the portal).
  astro::runSelfTests();
#endif

  gHostname = "overhead-" + chipSuffix();
  gHostname.toLowerCase();

  // Cold start: WiFi first (spec §13). If unprovisioned/out of range it opens the
  // captive portal — but a screen tap drops straight to OFFLINE field mode (cached
  // data), so the device is usable away from any network.
  splash("Connecting WiFi…");
  String apName = "Overhead-Setup-" + chipSuffix();
#if defined(OVERHEAD_BRINGUP_OFFLINE)
  // Board bring-up: don't let the captive portal block setup() (the panel/UI never
  // render until loop() runs). Boot straight to OFFLINE field mode.
  bool wifiOk = false; (void)apName;
#else
  bool wifiOk = prov.begin(apName, 300,   // 5 min: field testers ran out of the old 3-min window mid-setup
      []() -> bool { int16_t tx, ty; return touch.read(display, tx, ty); },   // tap = go offline
      [apName]() { splashPortal(apName); },                                   // portal opened -> prompt
      [](bool m, bool iv) {                                                   // mirror/invert from the portal
        int rot = m ? (DISPLAY_DEFAULT_ROTATION ^ 4) : (DISPLAY_DEFAULT_ROTATION & 7);   // mirror = rot's 0x4 bit
        settings.set("dispRotation", (long)rot); settings.set("dispInvert", iv); settings.save();
        display.applyDisplayPrefs(rot, iv);
      });
#endif
  gOffline = !wifiOk;
  WiFi.setAutoReconnect(true);            // let the stack retry drops on its own too
  WiFi.persistent(true);

  // Services.
  rtc.begin();
  timeSvc.setBus(&bus);
  timeSvc.setRtc(&rtc);
  timeSvc.begin();

#if defined(BOARD_HAS_PSRAM)
  // Route TLS allocations to PSRAM BEFORE the first HTTPS fetch (see note at top).
  mbedtls_platform_set_calloc_free(mbedtlsPsramCalloc, free);
#endif
  net.begin(24576);   // generous stack — mbedtls TLS handshakes are stack-hungry
  // The NetTask (core 0, just above idle) does multi-second blocking HTTP/TLS
  // reads. Arduino Stream reads busy-spin on yield() while waiting for TCP data, and
  // yield() will not run the LOWER-priority idle task — so IDLE0 starves and the task
  // watchdog aborts ("IDLE0 did not reset" -> boot loop). Nothing more important than
  // idle shares core 0, so unsubscribe core-0 idle from the TWDT. (loop/UI on core 1
  // stays watchdog-protected.)
  // Only the no-PSRAM CYD-class boards starve IDLE0 (slow single-core parse under
  // heap pressure). The S3 + PSRAM CrowPanels parse fast and never trip it — and there
  // disableCore0WDT() leaves the idle hook resetting an unsubscribed task, spamming
  // "esp_task_wdt_reset: task not found". So scope the workaround to non-PSRAM boards.
#if !defined(BOARD_HAS_PSRAM)
  disableCore0WDT();
#endif
  tleProv.begin(&settings, &net, &cache, &bus);    // cacheable -> boot-updater candidates (load cache + fetch if stale)
  launchProv.begin(&settings, &net, &cache, &bus); // LL2 + fallback
  spaceWxProv.begin(&settings, &net, &cache, &bus);// Kp/SFI (persists a while)

  web.setStatusJsonProvider(fillStatusJson);
  web.setDebug(&app, &display);     // /api/screen.bmp, /api/tap, /api/swipe
  web.begin(&settings, gHostname);  // registers routes only (no listener yet)
  // Field default: boot with the web server OFF so the AsyncTCP/server footprint
  // doesn't crowd the no-PSRAM heap floor — the live feeds get max contiguous heap.
  // Re-enable any time from the Health "Web" toggle or the serial console ("web on").
  // Boot with the web server OFF so the initial feed fetch keeps the full no-PSRAM heap floor; loop()
  // auto-starts it ~45 s in (once feeds settle), gated by webOnBoot (default ON). 'web on'/Health override.
  Serial.println("[web] booted OFF — auto-starts ~45s after boot unless webOnBoot=false");

  runBootUpdater();   // two-phase boot: refresh stale caches in a lean phase, then reboot to viewer

  locSvc.begin(&settings, &net, &bus, &timeSvc);   // kicks off IP geolocation (viewer phase)
  aircraftProv.begin(&settings, &net, &bus, &locSvc);
  weatherProv.begin(&settings, &net, &cache, &bus, &locSvc);
  avwxProv.begin(&settings, &net, &cache, &bus, &locSvc);
  sndProv.begin(&settings, &net, &cache, &bus, &locSvc);
  hazProv.begin(&settings, &net, &cache, &bus, &locSvc);
  marsProv.begin(&settings, &net, &cache, &bus);
  pmapProv.begin(&settings, &net, &cache, &locSvc);

  // The location resolves asynchronously a few seconds after boot; kick the
  // location-dependent providers as soon as it does, rather than waiting for
  // their (long) refresh interval — e.g. the sounding's is 60 min.
  bus.subscribe([](ProviderId id) {
    if (id == ProviderId::Time) {            // NTP just landed -> proper TTL re-check now the clock is valid
      tleProv.refresh(false);
      launchProv.refresh(false);
      spaceWxProv.refresh(false);
    }
    if (id != ProviderId::Location) return;
    // ADS-B FIRST, while the heap is freshest: its TLS fetch needs the ~42 KB
    // contiguous block, and the String-heavy feeds below fragment the heap — so
    // queueing aircraft ahead of them gets the live radar its clean-heap window
    // (it's the feed most prone to the heap-floor httpsSkip). Combined with the
    // web-off-at-boot default, the first radar load now actually populates.
    aircraftProv.poll();
    weatherProv.refresh(true);
    avwxProv.refresh(true);
    sndProv.refresh(true);
    hazProv.refresh(true);
  });

  // Periodic maintenance.
  sched.every(60UL * 60UL * 1000UL, [] { locSvc.refresh(); }, /*runNow=*/false);          // hourly
  uint32_t tleMs = (uint32_t)settings.getInt("refreshTleHour", 12) * 3600UL * 1000UL;
  sched.every(tleMs, [] { tleProv.refresh(); }, /*runNow=*/false);
  uint32_t lchMs = (uint32_t)settings.getInt("refreshLaunchMin", 45) * 60UL * 1000UL;
  sched.every(lchMs, [] { launchProv.refresh(); }, /*runNow=*/false);
  uint32_t adsbMs = (uint32_t)settings.getInt("adsbPollSec", 5) * 1000UL;
  if (adsbMs < 2000) adsbMs = 2000;                // respect the 1 req/s cloud cap
  sched.every(adsbMs, [] { aircraftProv.poll(); }, /*runNow=*/true);
  uint32_t swxMs = (uint32_t)settings.getInt("refreshSpaceWxMin", 20) * 60UL * 1000UL;
  sched.every(swxMs, [] { spaceWxProv.refresh(); }, /*runNow=*/false);
  uint32_t wxMs = (uint32_t)settings.getInt("refreshWeatherMin", 45) * 60UL * 1000UL;
  sched.every(wxMs, [] { weatherProv.refresh(); }, /*runNow=*/false);
  uint32_t avMs = (uint32_t)settings.getInt("refreshAvWxMin", 12) * 60UL * 1000UL;
  sched.every(avMs, [] { avwxProv.refresh(); }, /*runNow=*/false);
  sched.every(60UL * 60UL * 1000UL, [] { sndProv.refresh(); }, /*runNow=*/false);   // hourly sounding
  sched.every(15UL * 60UL * 1000UL, [] { hazProv.refresh(); }, /*runNow=*/false);   // hazards
  sched.every(6UL * 60UL * 60UL * 1000UL, [] { marsProv.refresh(); }, /*runNow=*/false);  // rovers ~6h
  sched.every(45UL * 60UL * 1000UL, [] { pmapProv.refresh(); }, /*runNow=*/false);         // pressure map

  // UI carousel, ground->space order: Launches, Aircraft, Satellites, Diagnostics.
  agendaPage = new PageAgenda(timeSvc, locSvc, weatherProv, tleProv, launchProv, settings);
  launchesPage = new PageLaunches(launchProv, timeSvc, locSvc, weatherProv);
  aircraftPage = new PageAircraft(aircraftProv, avwxProv, locSvc, settings, airportDb);
  aviationPage = new PageAviation(avwxProv, sndProv, hazProv, weatherProv, pmapProv, locSvc, settings);
  satsPage = new PageSatellites(tleProv, locSvc, timeSvc, settings);
  solarPage = new PageSolarSystem(timeSvc, locSvc, settings, marsProv);
  starPage = new PageStarMap(timeSvc, locSvc, settings);
  spaceWxPage = new PageSpaceWx(spaceWxProv, timeSvc, locSvc);
  healthPage = new PageHealth(touch, timeSvc, locSvc, gHostname,
                              tleProv, launchProv, aircraftProv, spaceWxProv, weatherProv,
                              themeCtl, settings);
  healthPage->setWebPortal(&web);     // "Web" on/off toggle frees heap for the feeds
  // Carousel, ground->space order (spec §4.1): Agenda (home), Launches, Aircraft,
  // Satellites, Space Wx, Solar System, then Diagnostics.
  app.addPage(agendaPage);
  app.addPage(launchesPage);
  app.addPage(aircraftPage);
  app.addPage(aviationPage);
  app.addPage(satsPage);
  app.addPage(spaceWxPage);
  app.addPage(solarPage);
  app.addPage(starPage);
  app.addPage(healthPage);
  app.setClockOverlay(new ClockOverlay(timeSvc, settings));  // bouncing-clock screensaver (tap the time)
  app.setLocationPicker(&settings, &locSvc);                 // status-strip crosshair -> saved-locations
  app.setInactivityMs((uint32_t)settings.getInt("inactivitySec", 90) * 1000UL);
  app.begin();

  // Intelligent Focus + day/night theming (spec §7).
  themeCtl.begin(&timeSvc, &locSvc, &display, &settings, &app);
  morse.begin(&settings, &themeCtl);     // Morse-code alert beeper (gated by night tier + settings)
  app.setBeeper(&morse);
  director.begin(&app, &settings, &timeSvc, &locSvc, &tleProv, &launchProv, satsPage);
  director.setSpaceWx(&spaceWxProv);
  director.setAviation(&avwxProv);
  director.setAviationPage(aviationPage);
  director.setWeather(&weatherProv);
  director.setAircraft(&aircraftProv);

  Serial.printf("[boot] done. free heap=%u  largest=%u\n",
                Display::freeHeap(), Display::largestFreeBlock());
}

// Serial console commands (recovery + diagnostics). Most useful: re-enable the web
// server after it's been toggled off (so toggling it off can never lock you out).
//   web on | web off | web   (toggle) ;  heap ;  reboot
static void serviceSerialCmd() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n'); cmd.trim(); cmd.toLowerCase();
  if (cmd.startsWith("web")) {
    String a = cmd.substring(3); a.trim();
    bool wantOn = (a == "on") || (a != "off" && !web.running());   // explicit on, or toggle-from-off
    if (!wantOn) {
      web.stop();
    } else if (web.everStopped()) {       // re-enable after a stop -> reboot for a clean re-bind (:80 TIME_WAIT)
      settings.set("webOnBoot", true); settings.save();
      Serial.println("[cmd] web re-enable needs a clean re-bind -> rebooting"); delay(100); ESP.restart();
    } else {
      web.start();
    }
    Serial.printf("[cmd] web %s\n", web.running() ? "ON" : "OFF");
  } else if (cmd == "heap") {
    Serial.printf("[cmd] free=%u largestBlock=%u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  } else if (cmd.startsWith("morse")) {
    String w = cmd.substring(5); w.trim(); if (!w.length()) w = "test";
    morse.play(w);                        // unconditional bench test (bypasses the enable/night gate)
  } else if (cmd == "reboot") {
    Serial.println("[cmd] rebooting"); delay(50); ESP.restart();
  } else if (cmd == "shot") {
    // WiFi-independent screenshot: JPEG-encode the framebuffer and dump it base64 over
    // serial (decode on the host). Proof-of-display for board bring-up without WiFi.
    display.setShotsEnabled(true);
    display.requestShot();
    display.serviceShot();                          // encodes synchronously on this (UI) thread
    const uint8_t* p = display.jpeg(); size_t n = display.jpegLen();
    if (!p || !n) { Serial.println("[shot] FAILED (no jpeg)"); }
    else {
      static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      Serial.printf("[shot] BEGIN len=%u\n", (unsigned)n);
      String line; line.reserve(80);
      for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16;
        if (i + 1 < n) v |= (uint32_t)p[i + 1] << 8;
        if (i + 2 < n) v |= p[i + 2];
        line += b64[(v >> 18) & 63];
        line += b64[(v >> 12) & 63];
        line += (i + 1 < n) ? b64[(v >> 6) & 63] : '=';
        line += (i + 2 < n) ? b64[v & 63] : '=';
        if (line.length() >= 76) { Serial.println(line); line = ""; }
      }
      if (line.length()) Serial.println(line);
      Serial.println("[shot] END");
    }
    display.freeShot();
  } else if (cmd.length()) {
    Serial.printf("[cmd] unknown: '%s' (try: web on|off, heap, reboot, shot)\n", cmd.c_str());
  }
}

void loop() {
  uint32_t now = millis();
  serviceSerialCmd(); // console: web on|off, heap, reboot
  net.poll();        // dispatch completed HTTP jobs on the UI thread
  timeSvc.tick();    // detect NTP sync edge -> publish Time
  web.loop();        // ElegantOTA pump

  // Delayed web auto-start: bring the server up ~45 s after boot, once the initial feeds have settled
  // and freed the heap floor — unless the user turned it off (webOnBoot=false) or stopped it this boot.
  static bool webAutoStarted = false;
  if (!webAutoStarted && !gOffline && now > 45000 && !web.running() && !web.everStopped()
      && settings.getBool("webOnBoot", true)) {
    webAutoStarted = true;
    web.start();
    Serial.println("[web] auto-started after boot");
  }

  sched.tick(now);
  themeCtl.tick(now);   // day/night palette + backlight
  director.tick(now);   // Intelligent Focus
  morse.tick(now);      // non-blocking Morse alert playback
  app.tick(now);
  display.flushFramebuffer();   // RGB panels: push the drawn cache out to PSRAM for the scan-out

  // WiFi watchdog (headless device): the radio occasionally drops and doesn't come
  // back on its own, which kills OTA + the debug API. Nudge a reconnect after a few
  // seconds; if it's still down after ~90 s, reboot to recover. Suppressed in offline
  // field mode — there's no network to wait for, so a reboot would just loop.
  static uint32_t wifiDownSince = 0;
  static bool reconnTried = false;
  if (gOffline) { /* offline field mode: leave the radio alone, never reboot for WiFi */ }
  else if (WiFi.status() == WL_CONNECTED) { wifiDownSince = 0; reconnTried = false; }
  else {
    if (!wifiDownSince) wifiDownSince = now;
    uint32_t down = now - wifiDownSince;
    if (down > 8000 && !reconnTried) { WiFi.reconnect(); reconnTried = true; Serial.println("[wd] wifi reconnect"); }
    if (down > 90000) { Serial.println("[wd] wifi down 90s -> reboot"); delay(50); ESP.restart(); }
  }

  // Memory-pressure low-water: the worst contiguous block seen (vs the ~42KB TLS
  // floor) is the key health metric on this no-PSRAM board.
  { uint32_t blk = ESP.getMaxAllocHeap(); if (blk < gHeapBlkMin) gHeapBlkMin = blk; }

  // Loop-timing heartbeat: if dt spikes, the UI/touch loop is being starved.
  static uint32_t lastHb = 0, maxDt = 0;
  uint32_t dt = millis() - now;
  if (dt > maxDt) maxDt = dt;
  if (now - lastHb > 3000) { lastHb = now; Serial.printf("[loop] dt=%lums max=%lums heap=%u\n", (unsigned long)dt, (unsigned long)maxDt, (unsigned)ESP.getFreeHeap()); maxDt = 0; }
  delay(2);
}
