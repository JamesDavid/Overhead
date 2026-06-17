#pragma once
#include "../core/Page.h"
#include <Arduino.h>

class AviationWxProvider;
class SoundingProvider;
class HazardProvider;
class WeatherProvider;
class PressureMapProvider;
class LocationService;

// pages/PageAviation — aviation weather "brief" (spec §14). Three views toggled
// by centre tap: METAR (decoded card + raw METAR/TAF), Sounding (Skew-T-style
// temp/dewpoint profile + freezing level + winds aloft), Hazards (AIRMET/SIGMET +
// PIREP). Tap edges step METAR stations. Phase 1 + 2 + 2b.
class PageAviation : public Page {
public:
  PageAviation(AviationWxProvider& wx, SoundingProvider& snd, HazardProvider& haz,
               WeatherProvider& wxo, PressureMapProvider& pmap, LocationService& loc)
    : _wx(wx), _snd(snd), _haz(haz), _wxo(wxo), _pmap(pmap), _loc(loc) {}

  const char* title() const override { return "Aviation"; }
  void focusSpeci();              // Director: jump to the SPECI station's METAR view
  void onEnter(App& app) override { _dirty = _needClear = true; }
  void onData(App& app, ProviderId id) override;
  void onTouch(App& app, int x, int y) override;
  void tick(App& app, uint32_t nowMs) override;
  bool autoAdvance(App& app) override;

private:
  enum class View { Metar, Map, Taf, Sounding, Hazards, Trends, Pressure };
  void draw(App& app);
  void drawMetar(App& app);
  void drawMap(App& app);
  void drawTaf(App& app);
  void drawSounding(App& app);
  void drawHazards(App& app);
  void drawTrends(App& app);
  void drawPressure(App& app);

  AviationWxProvider& _wx;
  SoundingProvider&   _snd;
  HazardProvider&     _haz;
  WeatherProvider&    _wxo;    // Open-Meteo hourly series (area trends)
  PressureMapProvider& _pmap;  // major-airport METAR pressure/cloud map
  LocationService&    _loc;
  int   _presMode = 0;         // pressure-map mode: 0=hPa, 1=inHg, 2=cloud
  View  _view = View::Map;     // Map is the default Aviation view (then Metar/Sounding/Hazards)
  int   _sel = 0;
  int   _mapZoom = 0;          // airport-map zoom index (top-left badge cycles)
  int   _tourN = 0;            // attract-tour: stations stepped in this view
  bool  _dirty = true;
  bool  _needClear = true;
  bool  _wasEmpty = true;
  uint32_t _lastDraw = 0;
};
