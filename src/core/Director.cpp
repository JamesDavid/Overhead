#include "Director.h"
#include "App.h"
#include "../services/Settings.h"
#include "../services/TimeService.h"
#include "../services/LocationService.h"
#include "../providers/TleProvider.h"
#include "../providers/LaunchProvider.h"
#include "../providers/SpaceWxProvider.h"
#include "../pages/PageSatellites.h"
#include "../astro/Sun.h"
#include <ArduinoJson.h>
#include <time.h>

void Director::begin(App* app, Settings* s, TimeService* time, LocationService* loc,
                     TleProvider* tle, LaunchProvider* launch, PageSatellites* satPage) {
  _app = app; _s = s; _time = time; _loc = loc; _tle = tle; _launch = launch; _satPage = satPage;
}

// Find the soonest upcoming pass across watchlisted birds (heavy -> cached).
void Director::scanPasses() {
  _passAos = 0; _passBird = ""; _passMaxEl = 0;
  if (!_loc->active().valid || _tle->sats().empty() || !_time->synced()) return;
  _eng.setObserver(_loc->active().lat, _loc->active().lon, 0);
  int minEl = (int)_s->getInt("satMinEl", 10);
  time_t now = time(nullptr);

  JsonArray wl = _s->doc()["watchlist"].as<JsonArray>();
  const auto& sats = _tle->sats();
  for (JsonVariant v : wl) {
    String pre = (const char*)(v | "");
    if (!pre.length()) continue;
    for (const auto& s : sats) {
      if (!s.name.startsWith(pre)) continue;
      _eng.loadTle(s.name.c_str(), s.line1.c_str(), s.line2.c_str());
      astro::SatPass p = _eng.nextPass(now, (double)minEl, 40);
      if (p.valid && (_passAos == 0 || p.aos < _passAos)) {
        _passAos = p.aos; _passMaxEl = p.maxElDeg; _passBird = s.name;
      }
      break;                                   // one match per watchlist entry
    }
  }
}

int Director::ambientTarget() {
  bool night = false;
  if (_time->synced() && _loc->active().valid) {
    double alt = astro::sunAltitudeDeg(_time->julianDate(), _loc->active().lat, _loc->active().lon);
    night = alt < (double)_s->getInt("nightAmbientAlt", -12);
  }
  String t = night ? _s->getString("ambientNight", "Solar System")
                   : _s->getString("ambientDay", "Launches");
  return _app->pageIndexByTitle(t.c_str());
}

void Director::tick(uint32_t nowMs) {
  if (!_s->getBool("focusEnabled", true)) return;
  if (nowMs - _lastDecideMs < 3000) return;       // decide every ~3 s
  _lastDecideMs = nowMs;

  if (nowMs - _lastScanMs > 60000 || (_passAos == 0 && nowMs > 8000)) {
    scanPasses(); _lastScanMs = nowMs;
  }

  time_t now = time(nullptr);
  int passLead = (int)_s->getInt("passLeadMin", 5) * 60;
  bool passNow = _passAos && (long)(_passAos - now) < passLead && (long)(_passAos - now) > -300;

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

  // Interrupt: pass wins ties if it starts first.
  if (passNow && (!launchNow || _passAos <= lnet)) {
    bool onSat = (_app->activeIndex() == satIdx);
    if (_app->autoFocus(satIdx)) onSat = true;
    else if (!onSat) _app->setBadge(satIdx, true);
    if (onSat && _satPage && _passBird != _focusedBird) {
      _satPage->focusBird(_passBird); _focusedBird = _passBird;
    }
    return;
  }
  if (launchNow) {
    if (!_app->autoFocus(lchIdx) && _app->activeIndex() != lchIdx) _app->setBadge(lchIdx, true);
    return;
  }

  // Ambient resting default.
  _app->autoFocus(ambientTarget());
}
