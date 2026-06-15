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
#include "core/App.h"
#include "services/Settings.h"
#include "services/Cache.h"
#include "services/NetClient.h"
#include "services/TimeService.h"
#include "services/LocationService.h"
#include "services/Provisioning.h"
#include "services/WebPortal.h"
#include "providers/TleProvider.h"
#include "pages/PageDiag.h"
#include "pages/PageSatellites.h"
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
// --- services ---
static Settings        settings;
static Cache           cache;
static NetClient       net;
static TimeService     timeSvc;
static LocationService locSvc;
static Provisioning    prov;
static WebPortal       web;
static TleProvider     tleProv;
// --- pages ---
static String          gHostname;
static PageSatellites* satsPage = nullptr;
static PageDiag*       diag = nullptr;

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

  web.setStatusJsonProvider(fillStatusJson);
  web.begin(&settings, gHostname);

  // Periodic maintenance.
  sched.every(60UL * 60UL * 1000UL, [] { locSvc.refresh(); }, /*runNow=*/false);          // hourly
  uint32_t tleMs = (uint32_t)settings.getInt("refreshTleHour", 12) * 3600UL * 1000UL;
  sched.every(tleMs, [] { tleProv.refresh(); }, /*runNow=*/false);

  // UI: Satellites is the first real tab; Diagnostics rides alongside for now.
  satsPage = new PageSatellites(tleProv, locSvc, timeSvc, settings);
  diag = new PageDiag(timeSvc, locSvc, gHostname);
  app.addPage(satsPage);
  app.addPage(diag);
  app.begin();

  Serial.printf("[boot] done. free heap=%u  largest=%u\n",
                Display::freeHeap(), Display::largestFreeBlock());
}

void loop() {
  uint32_t now = millis();
  net.poll();        // dispatch completed HTTP jobs on the UI thread
  timeSvc.tick();    // detect NTP sync edge -> publish Time
  web.loop();        // ElegantOTA pump
  sched.tick(now);
  app.tick(now);
  delay(2);
}
