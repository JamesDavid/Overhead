#pragma once
#include "../core/Page.h"
#include <Arduino.h>
#include <vector>

class LaunchProvider;
class TimeService;
class LocationService;
class WeatherProvider;

// pages/PageLaunches — the Launches tab (spec §6). Two views (centre-tap toggles):
//  - Card: next-launch card (provider, vehicle, mission, pad, status pill, big
//    T-minus that respects net_precision) + a short upcoming list.
//  - Map:  a world map (coastline) with a marker at each upcoming launch site;
//    side-tap cycles the selected rocket, highlighting its site.
// Four bottom filter chips: time window (24h/7d/30d/all), TBD (hide/show NET-TBD),
// launch site and company. Side taps step the selection.
class PageLaunches : public Page {
public:
  PageLaunches(LaunchProvider& lp, TimeService& time, LocationService& loc, WeatherProvider& wx)
    : _lp(lp), _time(time), _loc(loc), _wx(wx) {}

  const char* title() const override { return "Launches"; }
  bool clockKeepLive() const override { return true; }   // T-minus keeps counting under the clock
  void onEnter(App& app) override;
  void cycleView(int dir) override;            // up/down swipe: card <-> map
  void onData(App& app, ProviderId id) override;
  void onTouch(App& app, int x, int y) override;
  void tick(App& app, uint32_t nowMs) override;
  String gridStatus() override;
  bool autoAdvance(App& app) override;

private:
  void draw(App& app);
  void drawCard(App& app);       // next-launch card + upcoming list
  void drawMap(App& app);        // world map with launch-site markers
  void drawMessage(App& app, const char* msg);
  void drawChips(App& app);      // bottom time + TBD + site + company filter chips
  void rebuildFilter();          // window + TBD + site/company filters; distinct lists
  void focusLaunch(const String& name);   // Agenda tap -> select this launch

  LaunchProvider& _lp;
  TimeService&    _time;
  LocationService& _loc;
  WeatherProvider& _wx;
  int   _sel = 0;
  bool  _map = false;                  // false = card view, true = map view
  int   _winIdx = 1;                   // time window: 0=24h, 1=7d, 2=30d, 3=all
  bool  _hideTbd = true;               // hide NET-TBD (no firm date) launches
  bool  _visOnly = false;              // show only launches possibly visible from here
  String _siteVal, _orgVal;            // active filter values ("" = all)
  std::vector<int> _filtered;          // launch indices passing all filters
  std::vector<String> _sites, _orgs;   // distinct values in the 7d/non-TBD window
  bool  _dirty = true;
  bool  _needClear = true;   // full-clear only on structural change (anti-flicker)
  uint32_t _lastDraw = 0;
};
