#pragma once
#include "../core/Page.h"
#include <Arduino.h>

class AviationWxProvider;
class SoundingProvider;
class HazardProvider;
class WeatherProvider;
class PressureMapProvider;
class LocationService;
class Settings;

// pages/PageAviation — aviation weather "brief" (spec §14). Three views toggled
// by centre tap: METAR (decoded card + raw METAR/TAF), Sounding (Skew-T-style
// temp/dewpoint profile + freezing level + winds aloft), Hazards (AIRMET/SIGMET +
// PIREP). Tap edges step METAR stations. Phase 1 + 2 + 2b.
class PageAviation : public Page {
public:
  PageAviation(AviationWxProvider& wx, SoundingProvider& snd, HazardProvider& haz,
               WeatherProvider& wxo, PressureMapProvider& pmap, LocationService& loc, Settings& settings)
    : _wx(wx), _snd(snd), _haz(haz), _wxo(wxo), _pmap(pmap), _loc(loc), _settings(settings) {}

  const char* title() const override { return "Aviation Wx"; }
  void focusSpeci();              // Director: jump to the SPECI station's METAR view
  void focusAlert(App& app) override { focusSpeci(); }   // badged grid tile -> the SPECI
  void onEnter(App& app) override;
  void onData(App& app, ProviderId id) override;
  void onTouch(App& app, int x, int y) override;
  void cycleView(int dir) override;           // up/down swipe -> next/prev view
  int  viewCount() const override { return 7; }
  int  viewIndex() const override {
    return _view == View::Map ? 0 : _view == View::Metar ? 1 : _view == View::Taf ? 2
         : _view == View::Sounding ? 3 : _view == View::Hazards ? 4 : _view == View::Trends ? 5 : 6;
  }
  void tick(App& app, uint32_t nowMs) override;
  bool autoAdvance(App& app) override;
  String gridStatus() override;          // nearest METAR: cat + temp/wind

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
  void stepView(int dir);                // advance the view (+1 next, -1 prev), skipping empty TAF
  bool enterTaf();                       // point _sel at a TAF-bearing field; false if none have one
  int  nextTaf(int from, int dir) const; // next station index with a TAF (wrapping), or -1

  AviationWxProvider& _wx;
  SoundingProvider&   _snd;
  HazardProvider&     _haz;
  WeatherProvider&    _wxo;    // Open-Meteo hourly series (area trends)
  PressureMapProvider& _pmap;  // major-airport METAR pressure/cloud map
  LocationService&    _loc;
  Settings&           _settings;
  int   _presMode = 0;         // pressure-map mode: 0=hPa, 1=inHg, 2=cloud
  bool  _pZoom = false;        // pressure-map tap-to-zoom (like the star map)
  float _pZoomT = 0;           // 0 = full extent, 1 = zoomed in
  int   _pZoomDir = 0;         // +1 zooming in, -1 zooming out, 0 idle
  int   _pFx = 0, _pFy = 0;    // zoom focus (absolute screen px) = the tapped point
  uint32_t _pZoomMs = 0;
  View  _view = View::Map;     // Map is the default Aviation view (then Metar/Sounding/Hazards)
  int   _sel = 0;
  int   _mapZoom = 0;          // airport-map zoom index (top-left badge cycles)
  int   _tourN = 0;            // attract-tour: stations stepped in this view
  bool  _dirty = true;
  bool  _needClear = true;
  bool  _wasEmpty = true;
  uint32_t _lastDraw = 0;
};
