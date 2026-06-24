#include "Provisioning.h"
#include <WiFi.h>
#include <WiFiManager.h>

bool Provisioning::begin(const String& apName, uint16_t timeoutSec,
                         std::function<bool()> skipRequested,
                         std::function<void()> onPortalStart,
                         std::function<void(bool, bool)> onPrefs) {
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setConfigPortalTimeout(timeoutSec);   // don't block forever if unattended
  wm.setConnectTimeout(20);
  wm.setConfigPortalBlocking(false);       // we drive the wait so a screen tap can bail to offline

  // Display-orientation toggles on the portal form, so a reflected-HUD / mirrored screen that's
  // unreadable on the glass can still be fixed from the phone before the device leaves setup.
  WiFiManagerParameter mirrorP("dispMirror", "Mirror screen (reflected HUD)", "", 2, " type='checkbox' value='1'");
  WiFiManagerParameter invertP("dispInvert", "Invert colours", "", 2, " type='checkbox' value='1'");
  wm.addParameter(&mirrorP);
  wm.addParameter(&invertP);
  if (onPrefs) wm.setSaveParamsCallback([&]() {
    onPrefs(strlen(mirrorP.getValue()) > 0, strlen(invertP.getValue()) > 0);
  });

  // autoConnect() returns quickly here: true if it joined with saved creds,
  // false if it had to open the captive portal (or there are no creds).
  bool ok = wm.autoConnect(apName.c_str());
  if (ok || WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected: %s  ip=%s\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    return true;
  }

  // Portal is up. Pump it until we connect, the idle timeout fires, or the user
  // asks to skip (tap the screen) and run the device offline on cached data.
  if (onPortalStart) onPortalStart();
  uint32_t start = millis();
  while (millis() - start < (uint32_t)timeoutSec * 1000UL) {
    wm.process();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[wifi] connected: %s  ip=%s\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      return true;
    }
    if (skipRequested && skipRequested()) {
      Serial.println("[wifi] user skipped portal — running offline (field mode)");
      wm.stopConfigPortal();
      return false;
    }
    delay(30);
  }
  Serial.println("[wifi] portal timed out — running offline");
  wm.stopConfigPortal();
  return false;
}
