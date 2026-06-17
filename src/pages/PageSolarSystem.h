#pragma once
#include "../core/Page.h"
#include "../astro/SolarSystem.h"
#include <Arduino.h>

class TimeService;
class LocationService;
class Settings;

// pages/PageSolarSystem — the Solar System tab (spec §6). No network: a horizon
// half-dome plotting Sun/Moon/planets by az/el, a list with alt/az + distance,
// and the Moon phase + illumination. Uses the permissive astro::SolarSystem.
// Tap edges to select a body; bottom-left badge cycles the filter (all / up /
// naked-eye). Recomputes slowly (positions drift over minutes).
class PageSolarSystem : public Page {
public:
  PageSolarSystem(TimeService& time, LocationService& loc, Settings& settings)
    : _time(time), _loc(loc), _settings(settings) {}

  const char* title() const override { return "Solar System"; }
  void onEnter(App& app) override;
  void onData(App& app, ProviderId id) override { _dirty = true; }
  void onTouch(App& app, int x, int y) override;
  void tick(App& app, uint32_t nowMs) override;
  bool autoAdvance(App& app) override;

private:
  static constexpr int kN = 9;          // Sun, Moon, Mercury..Neptune
  void recompute();
  void computeRST(int idx);             // rise/set/transit of body idx over next 24h
  void draw(App& app);
  void drawOrbit(App& app);             // top-down orrery view
  void drawJupiter(App& app);           // Galilean moons, tilted to the observer's sky
  void drawSaturn(App& app);            // rings, tilted to the observer's sky
  bool visible(int i) const;            // passes the current filter (sky-dome)
  struct OrbBody { bool minor; int idx; };
  static constexpr int kMaxOrb = 16;
  int buildOrbit(OrbBody* out, int maxN);  // planets (per scope) + enabled minor bodies
  int orbitVisibleCount();

  TimeService&     _time;
  LocationService& _loc;
  Settings&        _settings;

  astro::PlanetState _st[kN];
  struct RST { bool hasRise=false, hasSet=false, hasTransit=false;
               time_t rise=0, set=0, transit=0; float transitAlt=0; };
  RST   _rst;                            // rise/set/transit of the selected body
  int   _rstFor = -1;                    // which body _rst was computed for
  int   _sel = 0;
  int   _orbSel = 2;                     // selected orbit body (0..8, 2=Earth)
  int   _orbScope = 2;                   // orbit view: 0 inner (Me..Ma), 1 mid (..Saturn), 2 all (..Pluto)
  int   _view = 0;                       // 0 sky-dome, 1 orbits, 2 Jupiter moons, 3 Saturn rings
  static constexpr int kViews = 4;
  int   _tourN = 0;                      // attract-tour: items stepped in this view
  int   _filter = 1;                    // 0 all, 1 above-horizon, 2 naked-eye
  bool  _stars = false;                 // overlay Star Map stars + constellations on the sky-dome
  bool  _dirty = true;
  uint32_t _lastDraw = 0;
};
