#include "Director.h"
#include "App.h"
#include "../services/Settings.h"
#include "../services/TimeService.h"
#include "../services/LocationService.h"
#include "../providers/TleProvider.h"
#include "../providers/LaunchProvider.h"
#include "../providers/SpaceWxProvider.h"
#include "../providers/AviationWxProvider.h"
#include "../providers/WeatherProvider.h"
#include "../providers/AircraftProvider.h"
#include "../pages/PageSatellites.h"
#include "../pages/PageAviation.h"
#include "../astro/Sun.h"
#include "../astro/Time.h"
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

// Naked-eye-bright satellites (ISS / Tiangong / Hubble) vs amateur-radio birds.
static bool brightSat(const String& n) {
  return n.startsWith("ISS") || n.startsWith("CSS") || n.indexOf("TIANGONG") >= 0 || n.startsWith("HST");
}
static bool radioSat(const String& n) {
  static const char* p[] = {"ISS", "SO-", "AO-", "PO-", "RS-", "FO-", "CAS-", "XW-", "JO-", "LO-", "TO-", "TEVEL", "HADES", "MESAT", "UVSQ"};
  for (auto x : p) if (n.startsWith(x)) return true;
  return false;
}

void Director::begin(App* app, Settings* s, TimeService* time, LocationService* loc,
                     TleProvider* tle, LaunchProvider* launch, PageSatellites* satPage) {
  _app = app; _s = s; _time = time; _loc = loc; _tle = tle; _launch = launch; _satPage = satPage;
}

// Find the soonest pass still in progress or upcoming across watchlisted birds.
void Director::scanPasses() {
  _passAos = 0; _passLos = 0; _passBird = ""; _passMaxEl = 0; _passVisible = false; _passRadio = false; _passCloud = -1;
  if (!_loc->active().valid || _tle->sats().empty() || !_time->synced()) return;
  double lat = _loc->active().lat, lon = _loc->active().lon;
  _eng.setObserver(lat, lon, 0);
  int minEl = (int)_s->getInt("satMinEl", 10);
  time_t now = time(nullptr);

  JsonArray wl = _s->doc()["watchlist"].as<JsonArray>();
  const auto& sats = _tle->sats();
  for (JsonVariant v : wl) {
    String pre = (const char*)(v | "");
    if (!pre.length()) continue;
    for (const auto& s : sats) {
      if (!satNameMatches(s.name, pre)) continue;
      _eng.loadTle(s.name.c_str(), s.line1.c_str(), s.line2.c_str());
      // First pass whose LOS is still ahead (so a pass already in progress counts).
      time_t from = now - 1200;
      astro::SatPass p;
      for (int k = 0; k < 3; ++k) {
        p = _eng.nextPass(from, (double)minEl, 40);
        if (!p.valid || p.los > now) break;
        from = p.los + 60;
      }
      if (p.valid && p.los > now && (_passAos == 0 || p.aos < _passAos)) {
        _passAos = p.aos; _passLos = p.los; _passMaxEl = p.maxElDeg; _passBird = s.name;
        bool dark = astro::sunAltitudeDeg(astro::julianDate(p.tca), lat, lon) < -6.0;
        _passVisible = brightSat(s.name) && dark && _eng.observe(p.tca).sunlit;
        _passRadio = radioSat(s.name);
        if (_wx) _passCloud = _wx->cloudCoverAt(p.tca);   // viewability gate
      }
      break;                                   // one match per watchlist entry
    }
  }
  if (_passAos) Serial.printf("[director] pass %s aos %+lds maxEl %.0f%s%s\n",
      _passBird.c_str(), (long)(_passAos - now), _passMaxEl,
      _passVisible ? " VIS" : "", _passRadio ? " RF" : "");
}

void Director::tick(uint32_t nowMs) {
  if (!_s->getBool("focusEnabled", true)) return;
  if (nowMs - _lastDecideMs < 3000) return;       // decide every ~3 s
  _lastDecideMs = nowMs;

  // Heavy (nextPass x watchlist) — scan once at startup then every 60s. (Was
  // re-scanning every 3s whenever no pass was found, starving the UI/touch loop.)
  if (_lastScanMs == 0 ? nowMs > 8000 : nowMs - _lastScanMs > 60000) {
    scanPasses(); _lastScanMs = nowMs;
  }

  time_t now = time(nullptr);
  int passLead = (int)_s->getInt("passLeadMin", 5) * 60;
  // Active from `passLead` before AOS until LOS (so an in-progress pass stays flagged).
  bool passNow = _passAos && now >= _passAos - passLead && now <= _passLos;

  time_t lnet = _launch->launches().empty() ? 0 : _launch->launches()[0].net;
  int launchLead = (int)_s->getInt("launchLeadMin", 10) * 60;
  bool launchNow = lnet && (long)(lnet - now) < launchLead && (long)(lnet - now) > -300;

  int satIdx = _app->pageIndexByTitle("Satellites");
  int lchIdx = _app->pageIndexByTitle("Launches");
  if (satIdx >= 0) _app->setBadge(satIdx, false);
  if (lchIdx >= 0) _app->setBadge(lchIdx, false);
  if (!passNow) _focusedBird = "";

  // Low-priority geomagnetic indicator: badge Space Wx when Kp is high (spec §7.2).
  int swxIdx = _app->pageIndexByTitle("Space Wx");
  if (swxIdx >= 0) _app->setBadge(swxIdx, _spacewx && _spacewx->kp() >= 5.0f);
  // Aviation notices: a SPECI special report, nearby EXTREME weather (thunderstorms
  // /hail/heavy precip/strong wind, now or in the TAF), or an active AIRMET/SIGMET/
  // PIREP. Badge persistently; announce the FIRST appearance of a new condition as a
  // brief cross-tab alert so a change to something severe is noticed.
  int avIdx = _app->pageIndexByTitle("Aviation Wx");
  bool avSpeci = _avwx && _avwx->hasSpeci();
  String avWx; bool avFcst = false;
  bool avExtreme = _avwx && _avwx->extremeWx(avWx, avFcst);
  bool avHaz = _avPage && _avPage->anyHazard();
  bool avNotice = avSpeci || avExtreme || avHaz;
  if (avIdx >= 0) _app->setBadge(avIdx, avNotice);
  String avKey = avExtreme ? avWx : avSpeci ? String("speci") : avHaz ? String("hazard") : String("");
  if (avKey != _avWxKey) {                          // rising edge -> brief announcement
    _avWxKey = avKey;
    if (avKey.length()) {
      _avAlertMsg = avExtreme ? ("WX " + avWx)
                  : avSpeci   ? String("WX: new SPECI report")
                              : String("WX: AIRMET/SIGMET nearby");
      _avAlertSetMs = nowMs;                        // shown for 9s (wrap-safe delta below)
    }
  }

  // Aircraft (nearly) overhead: cross-tab alert + Aircraft badge. Lower priority than an
  // imminent pass/launch (which return early below), so it shows in the quiet window.
  // Which pages may raise the cross-tab alert (configurable). Satellite + launch on by
  // default; aircraft-overhead off by default (it can be chatty); weather on.
  bool aSat = _s->getBool("alertSat", true), aLaunch = _s->getBool("alertLaunch", true);
  bool aAircraft = _s->getBool("alertAircraft", false), aWx = _s->getBool("alertWx", true);

  String ohMsg;
  int acIdx = _app->pageIndexByTitle("Aircraft");
  // No point alerting/badging about a plane overhead when you're already on the radar.
  bool overhead = aAircraft && _aircraft && _aircraft->overhead(ohMsg) && _app->activeIndex() != acIdx;
  if (acIdx >= 0) _app->setBadge(acIdx, overhead);

  // Interrupt: pass wins ties if it starts first. A specific item is highlighted
  // (the bird / launch) rather than touring, so hold the tour clock.
  if (passNow && aSat && (!launchNow || !aLaunch || _passAos <= lnet)) {
    _lastTourMs = nowMs;
    // Prominent cross-tab alert in the status strip (the badge alone was missable).
    long dt = (long)(_passAos - now);
    String a = _passBird + " " + (int)lround(_passMaxEl) + "\xF7 "
             + (dt > 0 ? "in " + String(dt / 60 + 1) + "m" : "NOW");
    if (_passVisible) a += (_passCloud >= 70) ? " VIS(cloud)" : " VIS";   // overcast = not worth it
    if (_passRadio)   a += " RF";
    _app->setAlert(a, satIdx);
    bool onSat = (_app->activeIndex() == satIdx);
    if (_app->autoFocus(satIdx)) onSat = true;
    else if (!onSat) _app->setBadge(satIdx, true);
    if (onSat && _satPage && _passBird != _focusedBird) {
      _satPage->focusBird(_passBird); _focusedBird = _passBird;
    }
    return;
  }
  if (launchNow && aLaunch) {
    _lastTourMs = nowMs;
    long dt = (long)(lnet - now);
    const Launch& L = _launch->launches()[0];          // the imminent launch (lnet came from it)
    String who = L.vehicle.length() ? L.vehicle : String("Launch");   // rocket name -> beep e.g. "Falcon"
    _app->setAlert(who + " " + (dt > 0 ? "in " + String(dt / 60 + 1) + "m" : "NOW"), lchIdx);
    if (!_app->autoFocus(lchIdx) && _app->activeIndex() != lchIdx) _app->setBadge(lchIdx, true);
    return;
  }
  // Nothing imminent: a plane overhead, else a fresh weather announcement, else clear.
  if (overhead)                                                  _app->setAlert("Overhead " + ohMsg, acIdx);
  else if (aWx && (nowMs - _avAlertSetMs) < 9000 && _avAlertMsg.length()) _app->setAlert(_avAlertMsg, avIdx);
  else _app->setAlert("");

  // Ambient resting default + multi-page attract tour. ambientDay/Night may be a
  // comma-separated rotation of page titles (e.g. "Solar System,Star Map"): once
  // settled on a page in AUTO, tour its items/views; when that page's tour wraps a
  // full cycle, advance to the next page in the rotation (spec §7).
  bool night = false;
  if (_time->synced() && _loc->active().valid) {
    double alt = astro::sunAltitudeDeg(_time->julianDate(), _loc->active().lat, _loc->active().lon);
    night = alt < (double)_s->getInt("nightAmbientAlt", -12);
  }
  if (night != _ambNight) { _ambNight = night; _ambPos = 0; _lastTourMs = nowMs; }

  String spec = night ? _s->getString("ambientNight", "Solar System,Star Map")
                      : _s->getString("ambientDay", "Agenda");
  int pages[10]; int np = 0, nResolved = 0;         // resolved rotation page indices
  for (int start = 0; np < 10 && start <= (int)spec.length(); ) {
    int comma = spec.indexOf(',', start);
    if (comma < 0) comma = spec.length();
    String t = spec.substring(start, comma); t.trim();
    int idx = t.length() ? _app->pageIndexByTitle(t.c_str()) : -1;
    if (idx >= 0) {
      nResolved++;
      if (!_app->pageAutoSkip(idx)) pages[np++] = idx;   // skip a down-feed page in the tour
    }
    if (comma >= (int)spec.length()) break;
    start = comma + 1;
  }
  // If EVERY rotation page is currently skippable, don't get stuck — fall back to the
  // full resolved list so the tour still moves.
  if (np == 0 && nResolved > 0) {
    for (int start = 0; np < 10 && start <= (int)spec.length(); ) {
      int comma = spec.indexOf(',', start); if (comma < 0) comma = spec.length();
      String t = spec.substring(start, comma); t.trim();
      int idx = t.length() ? _app->pageIndexByTitle(t.c_str()) : -1;
      if (idx >= 0) pages[np++] = idx;
      if (comma >= (int)spec.length()) break; start = comma + 1;
    }
  }
  // Fold badged "notice" pages into the AUTO rotation so it periodically visits the
  // flag (Kp storm / Aviation SPECI) and returns, until it clears. (In MANUAL the
  // badge is the only signal — autoFocus is a no-op there.)
  auto addNotice = [&](int idx, bool on) {
    if (!on || idx < 0 || np >= 10) return;
    for (int i = 0; i < np; ++i) if (pages[i] == idx) return;   // dedupe
    pages[np++] = idx;
  };
  addNotice(swxIdx, _spacewx && _spacewx->kp() >= 5.0f);
  addNotice(avIdx, avNotice);
  if (np == 0) return;
  if (_ambPos >= np) _ambPos = 0;
  int amb = pages[_ambPos];
  bool switched = _app->autoFocus(amb);
  if (switched && amb == avIdx && _avPage) {           // landed on Aviation -> the relevant wx view
    if (avSpeci) _avPage->focusSpeci(); else _avPage->focusWxNotice();
  }

  uint32_t dwell = (uint32_t)_s->getInt("tourDwellSec", 6) * 1000UL;
  if (_app->mode() == App::Mode::Auto && !_app->pinned() && _app->activeIndex() == amb) {
    if (nowMs - _lastTourMs >= dwell) {
      bool cycled = _app->autoAdvanceActive();
      _lastTourMs = nowMs;
      if (cycled && np > 1) _ambPos = (_ambPos + 1) % np;   // page toured -> next in rotation
    }
  } else {
    _lastTourMs = nowMs;     // (re)entering, or user took over -> dwell fully first
  }
}
