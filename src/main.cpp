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
#include "services/Settings.h"
#include "services/Cache.h"
#include "services/NetClient.h"
#include "services/TimeService.h"
#include "services/LocationService.h"
#include "services/Provisioning.h"
#include "services/WebPortal.h"
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
#include "pages/PageAviation.h"
#if ASTRO_SELFTEST
#include "astro/SelfTest.h"
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
static Director  director;
// --- services ---
static Settings        settings;
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
// --- pages ---
static String          gHostname;
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
}

static void fillStatusJson(JsonDocument& d) {
  d["fw"]       = OVERHEAD_FW_VERSION;
  d["board"]    = BOARD_NAME;
  d["uptimeS"]  = (uint32_t)(millis() / 1000);
  d["heap"]     = Display::freeHeap();
  d["heapBlk"]  = Display::largestFreeBlock();
  d["psram"]    = Display::psramSize();
  d["wifi"]     = WiFi.status() == WL_CONNECTED;
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
}

void setup() {
  Serial.begin(OVERHEAD_LOG_BAUD);
  delay(200);
  Serial.println("\n\n=== Overhead — Milestone 1 (services + infra) ===");
  Serial.printf("board: %s  fw: %s\n", BOARD_NAME, OVERHEAD_FW_VERSION);

  if (!LittleFS.begin(true)) Serial.println("[fs] LittleFS mount FAILED");
  settings.begin();
  cache.begin();

  if (!display.begin()) Serial.println("[display] init FAILED");
  touch.begin(display);

#if ASTRO_SELFTEST
  // Validate the astro core before the network (which may block on the portal).
  astro::runSelfTests();
#endif

  gHostname = "overhead-" + chipSuffix();
  gHostname.toLowerCase();

  // Cold start: WiFi first (spec §13). Blocks on the captive portal if unprovisioned.
  splash("Connecting WiFi…");
  prov.begin("Overhead-Setup-" + chipSuffix());

  // Services.
  rtc.begin();
  timeSvc.setBus(&bus);
  timeSvc.setRtc(&rtc);
  timeSvc.begin();

  net.begin();
  locSvc.begin(&settings, &net, &bus, &timeSvc);   // kicks off IP geolocation
  tleProv.begin(&settings, &net, &cache, &bus);    // loads cache + fetches TLEs
  launchProv.begin(&settings, &net, &cache, &bus); // LL2 + fallback
  aircraftProv.begin(&settings, &net, &bus, &locSvc);
  spaceWxProv.begin(&settings, &net, &cache, &bus);
  weatherProv.begin(&settings, &net, &cache, &bus, &locSvc);
  avwxProv.begin(&settings, &net, &cache, &bus, &locSvc);
  sndProv.begin(&settings, &net, &cache, &bus, &locSvc);
  hazProv.begin(&settings, &net, &cache, &bus, &locSvc);

  web.setStatusJsonProvider(fillStatusJson);
  web.begin(&settings, gHostname);

  // The location resolves asynchronously a few seconds after boot; kick the
  // location-dependent providers as soon as it does, rather than waiting for
  // their (long) refresh interval — e.g. the sounding's is 60 min.
  bus.subscribe([](ProviderId id) {
    if (id != ProviderId::Location) return;
    weatherProv.refresh(true);
    avwxProv.refresh(true);
    sndProv.refresh(true);
    hazProv.refresh(true);
    aircraftProv.poll();
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

  // UI carousel, ground->space order: Launches, Aircraft, Satellites, Diagnostics.
  agendaPage = new PageAgenda(timeSvc, locSvc, weatherProv, tleProv, launchProv, settings);
  launchesPage = new PageLaunches(launchProv, timeSvc);
  aircraftPage = new PageAircraft(aircraftProv, locSvc, settings);
  aviationPage = new PageAviation(avwxProv, sndProv, hazProv, locSvc);
  satsPage = new PageSatellites(tleProv, locSvc, timeSvc, settings);
  solarPage = new PageSolarSystem(timeSvc, locSvc, settings);
  starPage = new PageStarMap(timeSvc, locSvc);
  spaceWxPage = new PageSpaceWx(spaceWxProv, timeSvc, locSvc);
  healthPage = new PageHealth(touch, timeSvc, locSvc, gHostname,
                              tleProv, launchProv, aircraftProv, spaceWxProv, weatherProv);
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
  app.setInactivityMs((uint32_t)settings.getInt("inactivitySec", 90) * 1000UL);
  app.begin();

  // Intelligent Focus + day/night theming (spec §7).
  themeCtl.begin(&timeSvc, &locSvc, &display, &settings, &app);
  director.begin(&app, &settings, &timeSvc, &locSvc, &tleProv, &launchProv, satsPage);
  director.setSpaceWx(&spaceWxProv);
  director.setAviation(&avwxProv);

  Serial.printf("[boot] done. free heap=%u  largest=%u\n",
                Display::freeHeap(), Display::largestFreeBlock());
}

void loop() {
  uint32_t now = millis();
  net.poll();        // dispatch completed HTTP jobs on the UI thread
  timeSvc.tick();    // detect NTP sync edge -> publish Time
  web.loop();        // ElegantOTA pump
  sched.tick(now);
  themeCtl.tick(now);   // day/night palette + backlight
  director.tick(now);   // Intelligent Focus
  app.tick(now);

  // Loop-timing heartbeat: if dt spikes, the UI/touch loop is being starved.
  static uint32_t lastHb = 0, maxDt = 0;
  uint32_t dt = millis() - now;
  if (dt > maxDt) maxDt = dt;
  if (now - lastHb > 3000) { lastHb = now; Serial.printf("[loop] dt=%lums max=%lums heap=%u\n", (unsigned long)dt, (unsigned long)maxDt, (unsigned)ESP.getFreeHeap()); maxDt = 0; }
  delay(2);
}
