#pragma once
#include "../core/Page.h"
#include "../astro/SolarSystem.h"
#include <Arduino.h>

class TimeService;
class LocationService;
class Settings;
class MarsProvider;

// pages/PageSolarSystem — the Solar System tab (spec §6). No network: a horizon
// half-dome plotting Sun/Moon/planets by az/el, a list with alt/az + distance,
// and the Moon phase + illumination. Centre tap tours the whole system from Earth
// outward: sky-dome -> orbits -> Moon -> Mars -> Jupiter -> Saturn -> Deep Space ->
// meteors. Tap edges to select a body; bottom-left badge cycles the filter.
class PageSolarSystem : public Page {
public:
  PageSolarSystem(TimeService& time, LocationService& loc, Settings& settings, MarsProvider& mars)
    : _time(time), _loc(loc), _settings(settings), _mars(mars) {}

  const char* title() const override { return "Solar System"; }
  void onEnter(App& app) override;
  void onData(App& app, ProviderId id) override { _dirty = true; }
  void onTouch(App& app, int x, int y) override;
  void cycleView(int dir) override;            // up/down swipe: step the 8 sub-views
  int  viewCount() const override { return kViews; }
  int  viewIndex() const override { return _view; }
  void tick(App& app, uint32_t nowMs) override;
  bool autoAdvance(App& app) override;
  String gridStatus() override;          // Moon illumination %

private:
  static constexpr int kN = 9;          // Sun, Moon, Mercury..Neptune
  void recompute();
  void computeRST(int idx);             // rise/set/transit of body idx over next 24h
  void draw(App& app);
  void drawOrbit(App& app);             // top-down orrery view
  void drawMoon(App& app);              // Moon phase + near/far-side landing-site map
  void drawMars(App& app);              // Mars distance + surface map + rover status
  void drawJupiter(App& app);           // Galilean moons, tilted to the observer's sky
  void drawSaturn(App& app);            // rings, tilted to the observer's sky
  void drawDeepSpace(App& app);         // iconic active deep-space missions
  void drawShowers(App& app);           // upcoming meteor showers + visibility
  // sub-Earth/sub-point overlay on an equirectangular body map (Moon/Mars).
  void drawBodyOverlay(App& app, int mx, int my, int mw, int mh, double lonMin,
                       double lonMax, double slat, double slon, bool boundary,
                       uint16_t col, const char* label);
  bool visible(int i) const;            // passes the current filter (sky-dome)
  struct OrbBody { bool minor; int idx; };
  static constexpr int kMaxOrb = 16;
  int buildOrbit(OrbBody* out, int maxN);  // planets (per scope) + enabled minor bodies
  int orbitVisibleCount();

  TimeService&     _time;
  LocationService& _loc;
  Settings&        _settings;
  MarsProvider&    _mars;

  astro::PlanetState _st[kN];
  struct RST { bool hasRise=false, hasSet=false, hasTransit=false;
               time_t rise=0, set=0, transit=0; float transitAlt=0; };
  RST   _rst;                            // rise/set/transit of the selected body
  int   _rstFor = -1;                    // which body _rst was computed for
  int   _sel = 0;
  int   _orbSel = 2;                     // selected orbit body (0..8, 2=Earth)
  int   _orbScope = 2;                   // orbit view: 0 inner (Me..Ma), 1 mid (..Saturn), 2 all (..Pluto)
  int   _view = 0;                       // 0 sky,1 orbits,2 Moon,3 Mars,4 Jupiter,5 Saturn,6 deep,7 showers
  static constexpr int kViews = 8;
  bool  _moonFar = false;                // Moon view: false=near side, true=far side
  double _zoomAu = 0;                    // orbits: eased plot scale (AU at the edge)
  bool  _animating = false;              // orbits: a zoom transition is in progress
  int   _tourN = 0;                      // attract-tour: items stepped in this view
  int   _filter = 1;                    // 0 all, 1 above-horizon, 2 naked-eye
  bool  _stars = false;                 // overlay Star Map stars + constellations on the sky-dome
  bool  _dirty = true;
  uint32_t _lastDraw = 0;
};
