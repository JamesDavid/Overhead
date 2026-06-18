#pragma once
#include <Arduino.h>

// assets/LaunchSites — a compact gazetteer of active orbital launch sites, used to
// plot upcoming launches on the Launches map view. The Launch Library "list" mode
// flattens the pad to a string (no lat/lon), and fetching detailed mode would blow
// the no-PSRAM heap budget — so we resolve coordinates here by substring-matching
// the launch "location" string (e.g. "Cape Canaveral SFS, FL, USA"). Keys are
// ordered most-specific first; first match wins. country is used for the filter.
struct LaunchSite { const char* key; float lat; float lon; const char* country; };

static const LaunchSite kLaunchSites[] = {
  {"Cape Canaveral",            28.49f,  -80.58f, "USA"},
  {"Kennedy",                   28.57f,  -80.65f, "USA"},
  {"Vandenberg",                34.74f, -120.57f, "USA"},
  {"Starbase",                  25.997f, -97.155f,"USA"},
  {"Boca Chica",                25.997f, -97.155f,"USA"},
  {"Wallops",                   37.94f,  -75.47f, "USA"},
  {"Mid-Atlantic Regional",     37.94f,  -75.47f, "USA"},
  {"Pacific Spaceport",         57.44f, -152.34f, "USA"},
  {"Kodiak",                    57.44f, -152.34f, "USA"},
  {"Guiana",                     5.236f, -52.768f,"France"},
  {"Kourou",                     5.236f, -52.768f,"France"},
  {"Baikonur",                  45.965f,  63.305f,"Kazakhstan"},
  {"Vostochny",                 51.884f, 128.334f,"Russia"},
  {"Plesetsk",                  62.926f,  40.577f,"Russia"},
  {"Kapustin Yar",              48.57f,   46.30f, "Russia"},
  {"Jiuquan",                   40.958f, 100.291f,"China"},
  {"Xichang",                   28.246f, 102.026f,"China"},
  {"Taiyuan",                   38.849f, 111.608f,"China"},
  {"Wenchang",                  19.614f, 110.951f,"China"},
  {"Tanegashima",               30.40f,  130.97f, "Japan"},
  {"Uchinoura",                 31.251f, 131.079f,"Japan"},
  {"Satish Dhawan",             13.733f,  80.235f,"India"},
  {"Sriharikota",               13.733f,  80.235f,"India"},
  {"Rocket Lab Launch Complex 1",-39.26f,177.865f,"New Zealand"},
  {"Mahia",                    -39.26f,  177.865f,"New Zealand"},
  {"Palmachim",                 31.884f,  34.680f,"Israel"},
  {"Naro",                      34.431f, 127.535f,"South Korea"},
  {"Sohae",                     39.66f,  124.705f,"North Korea"},
  {"Imam Khomeini",             35.234f,  53.921f,"Iran"},
  {"Semnan",                    35.234f,  53.921f,"Iran"},
  {"SaxaVord",                  60.69f,   -0.77f, "UK"},
  {"Sutherland",                58.49f,   -4.00f, "UK"},
  {"Esrange",                   67.89f,   21.10f, "Sweden"},
  {"Andoya",                    69.29f,   16.02f, "Norway"},
};
static const int kLaunchSiteCount = sizeof(kLaunchSites) / sizeof(kLaunchSites[0]);

// Resolve a launch "location" string to coordinates + country. Returns false if no
// site key matched. country falls back to the text after the last comma when unmatched.
static inline bool launchSiteLatLon(const String& location, float& lat, float& lon,
                                    String& country) {
  for (int i = 0; i < kLaunchSiteCount; ++i)
    if (location.indexOf(kLaunchSites[i].key) >= 0) {
      lat = kLaunchSites[i].lat; lon = kLaunchSites[i].lon;
      country = kLaunchSites[i].country;
      return true;
    }
  int c = location.lastIndexOf(',');                 // fall back to trailing field
  country = (c >= 0) ? location.substring(c + 1) : location;
  country.trim();
  return false;
}

// Country alone (for the filter chip), without needing coordinates.
static inline String launchSiteCountry(const String& location) {
  float a, b; String c; launchSiteLatLon(location, a, b, c); return c;
}

// Representative launch-corridor azimuth (deg clockwise from N) for well-documented
// sites; 0 = unknown (draw no arc). These are real range-safety corridors but per-SITE
// (not per-mission) — the light feed has no orbit, so it's an approximation. (Per-
// mission azimuth needs mode=normal -> PSRAM boards only; see BACKLOG M4.)
static inline int launchSiteAz(const String& location) {
  struct Az { const char* key; int az; };
  static const Az kAz[] = {
    {"Cape Canaveral", 95}, {"Kennedy", 95}, {"Starbase", 95}, {"Boca Chica", 95}, // E over Atlantic/Gulf
    {"Vandenberg", 190}, {"Pacific Spaceport", 190}, {"Kodiak", 190},               // S (polar/SSO)
    {"Wallops", 130}, {"Mid-Atlantic", 130},                                        // SE down the Atlantic
    {"Guiana", 90}, {"Kourou", 90},                                                 // due E (near-equator)
    {"Baikonur", 40},                                                               // NE (51.6 deg)
    {"Tanegashima", 110}, {"Uchinoura", 110},                                       // ESE over the Pacific
    {"Satish Dhawan", 105}, {"Sriharikota", 105},                                   // E (with dogleg)
    {"Wenchang", 100}, {"Taiyuan", 190}, {"Palmachim", 285},                        // Hainan E / SSO S / W retro
  };
  for (auto& a : kAz) if (location.indexOf(a.key) >= 0) return a.az;
  return 0;
}
