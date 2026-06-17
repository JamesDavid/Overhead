#pragma once
#include <stdint.h>

// core/Ids.h — stable identifiers shared across the EventBus, providers, and
// pages. Providers publish a ProviderId when their model updates; pages and the
// Director (later) subscribe and pull the latest model from the provider.

enum class ProviderId : uint8_t {
  Time,        // TimeService: NTP sync / tz changed
  Location,    // LocationService: active observer location changed
  Launch,      // LaunchProvider (milestone 4)
  Tle,         // TleProvider   (milestone 3)
  Aircraft,    // AircraftProvider (milestone 5)
  SpaceWx,     // SpaceWxProvider  (milestone 8)
  Weather,     // WeatherProvider  (milestone 10)
  Mars,        // MarsProvider (rover summaries)
  _count
};

inline const char* providerName(ProviderId id) {
  switch (id) {
    case ProviderId::Time:     return "Time";
    case ProviderId::Location: return "Location";
    case ProviderId::Launch:   return "Launch";
    case ProviderId::Tle:      return "Tle";
    case ProviderId::Aircraft: return "Aircraft";
    case ProviderId::SpaceWx:  return "SpaceWx";
    case ProviderId::Weather:  return "Weather";
    case ProviderId::Mars:     return "Mars";
    default:                   return "?";
  }
}
