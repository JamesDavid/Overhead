#pragma once
#include "../core/Page.h"
#include <Arduino.h>
#include <vector>

class LaunchProvider;
class TimeService;

// pages/PageLaunches — the Launches tab (spec §6). Two views (centre-tap toggles):
//  - Card: next-launch card (provider, vehicle, mission, pad, status pill, big
//    T-minus that respects net_precision) + a short upcoming list.
//  - Map:  a world map (coastline) with a marker at each upcoming launch site;
//    side-tap cycles the selected rocket, highlighting its site.
// The window is fixed at 7 days and NET-TBD launches are hidden; two filter chips
// (launch site / company) narrow the set. Side taps step the selection.
class PageLaunches : public Page {
public:
  PageLaunches(LaunchProvider& lp, TimeService& time) : _lp(lp), _time(time) {}

  const char* title() const override { return "Launches"; }
  void onEnter(App& app) override { _dirty = _needClear = true; }
  void onData(App& app, ProviderId id) override;
  void onTouch(App& app, int x, int y) override;
  void tick(App& app, uint32_t nowMs) override;
  bool autoAdvance(App& app) override;

private:
  void draw(App& app);
  void drawCard(App& app);       // next-launch card + upcoming list
  void drawMap(App& app);        // world map with launch-site markers
  void drawMessage(App& app, const char* msg);
  void drawChips(App& app);      // bottom site + company filter chips
  void rebuildFilter();          // 7d + non-TBD + site/company filters; distinct lists

  LaunchProvider& _lp;
  TimeService&    _time;
  int   _sel = 0;
  bool  _map = false;                  // false = card view, true = map view
  String _siteVal, _orgVal;            // active filter values ("" = all)
  std::vector<int> _filtered;          // launch indices passing all filters
  std::vector<String> _sites, _orgs;   // distinct values in the 7d/non-TBD window
  bool  _dirty = true;
  bool  _needClear = true;   // full-clear only on structural change (anti-flicker)
  uint32_t _lastDraw = 0;
};
