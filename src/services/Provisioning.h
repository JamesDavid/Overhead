#pragma once
#include <Arduino.h>
#include <functional>

// services/Provisioning — first-boot WiFi via WiFiManager captive portal
// (spec §3.1, §13). No hardcoded creds: if none are stored it opens an AP +
// captive portal, then persists creds in NVS. Returns true once connected, false
// if the user dropped to offline (field) mode or the portal timed out. This runs
// BEFORE the async WebPortal starts.
class Provisioning {
public:
  // apName: SoftAP SSID for the config portal. timeoutSec: portal idle timeout.
  // skipRequested: polled while the portal is up — return true to abandon it and
  //   run offline (e.g. a screen tap). onPortalStart: called once if the captive
  //   portal actually opens (saved creds failed/absent) so the caller can prompt.
  // onPrefs: called with (mirror, invert) when the portal form is saved, so a reflected/unreadable
  //   screen can be corrected from the phone during first-boot setup (the caller applies + persists).
  bool begin(const String& apName, uint16_t timeoutSec = 180,
             std::function<bool()> skipRequested = nullptr,
             std::function<void()> onPortalStart = nullptr,
             std::function<void(bool mirror, bool invert)> onPrefs = nullptr);
};
