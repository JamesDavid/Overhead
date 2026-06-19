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
  bool anyHazard() const;         // any AIRMET/SIGMET/PIREP loaded? (Director notice)
  void focusWxNotice();           // Director: land on the most relevant wx view (hazards/metar)
  void onEnter(App& app) override;
  void onData(App& app, ProviderId id) override;
  void onTouch(App& app, int x, int y) override;
  void cycleView(int dir) override;           // up/down swipe -> next/prev view
  int  viewCount() const override;            // 7, or 6 when no field has a TAF (TAF view is skipped)
  int  viewIndex() const override;
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
  void stepView(int dir);                // advance the view (+1 next, -1 prev), skipping empty views
  int  activeViews(View* out) const;     // ordered visible views (TAF/Hazards hidden when empty)
  void drillPressure(App& app, int absX, int absY);  // US/world map: fetch local airports around a tap
  bool anyTaf() const;                   // any loaded field currently carries a TAF?
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
  // pressure-map tap-to-zoom: discrete levels cycled by tapping the map (off, then a
  // few fixed magnifications about the tapped point).
  static constexpr int kPZoomN = 4;   // level 0 = off, then 3 zoom factors (see kPZoomF)
  int   _pZoomLevel = 0;       // current level index (0 = full extent)
  float _pZoomCur = 1.f;       // animated displayed factor (1 = full extent)
  float _pZoomFrom = 1.f;      // factor at the start of the current animation
  bool  _pZoom = false;        // zoomed in at all (level > 0)
  int   _pFx = 0, _pFy = 0;    // zoom focus (absolute screen px) = the tapped point
  uint32_t _pZoomMs = 0;
  void  cyclePresZoom(int fx, int fy);   // advance to the next zoom level about (fx,fy)
  void  resetPresZoom() { _pZoomLevel = 0; _pZoomCur = 1.f; _pZoomFrom = 1.f; _pZoom = false; }
  uint32_t _presRetryMs = 0;   // retry an empty (un-cached) pressure scope
  static constexpr int kMChips = 8;        // METAR field-selector chips (reuse App::drawChipRow)
  int   _mChipX[kMChips] = {0}, _mChipW[kMChips] = {0}, _mChipN = 0;
  View  _view = View::Map;     // Map is the default Aviation view (then Metar/Sounding/Hazards)
  int   _sel = 0;
  int   _mapZoom = 0;          // airport-map zoom index (top-left badge cycles)
  int   _tourN = 0;            // attract-tour: stations stepped in this view
  bool  _dirty = true;
  bool  _needClear = true;
  bool  _wasEmpty = true;
  uint32_t _lastDraw = 0;
};
