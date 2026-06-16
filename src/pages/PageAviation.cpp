#include "PageAviation.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../providers/AviationWxProvider.h"
#include "../providers/SoundingProvider.h"
#include "../providers/HazardProvider.h"
#include "../services/LocationService.h"
#include <math.h>

static constexpr double D2R = 3.14159265358979323846 / 180.0;

static Color catColor(const String& c) {
  if (c == "VFR")  return gTheme.ok;
  if (c == "MVFR") return gTheme.accent;
  return gTheme.warn;   // IFR / LIFR
}

// Airport-map zoom: multiplier on the fetch-bbox half-ranges. ~57nm (full) down
// to ~13nm (tight, zoomed in on the observer). Tap the top-left badge to cycle.
static const float kMapZoom[] = { 1.0f, 0.6f, 0.38f, 0.22f };
static constexpr int kMapZoomN = 4;

static int drawWrapped(LGFX& g, const String& text, int x, int y, int maxChars, int maxLines, Color c) {
  g.setTextColor(c, gTheme.bg);
  g.setTextDatum(textdatum_t::top_left);
  int line = 0, i = 0, n = text.length();
  while (i < n && line < maxLines) {
    int end = i + maxChars;
    if (end >= n) end = n; else { int sp = text.lastIndexOf(' ', end); if (sp > i) end = sp; }
    g.drawString(text.substring(i, end), x, y);
    y += 11; line++;
    i = end + (end < n && text[end] == ' ' ? 1 : 0);
  }
  return y;
}

void PageAviation::onData(App& app, ProviderId id) {
  int n = (int)_wx.stations().size();
  if (_sel >= n) _sel = n ? n - 1 : 0;
  bool empty = (n == 0);
  if (empty != _wasEmpty) { _wasEmpty = empty; }
  _dirty = _needClear = true;
}

void PageAviation::onTouch(App& app, int x, int y) {
  int third = app.contentW() / 3;
  if (x >= third && x <= 2 * third) {               // centre: cycle view (Map first)
    _view = _view == View::Map ? View::Metar : _view == View::Metar ? View::Sounding
          : _view == View::Sounding ? View::Hazards : View::Map;
    _needClear = _dirty = true; return;
  }
  if (_view == View::Map && x < 50 && y < 16) {     // top-left badge: cycle map zoom
    _mapZoom = (_mapZoom + 1) % kMapZoomN;
    _needClear = _dirty = true; return;
  }
  int n = (int)_wx.stations().size();
  if (n && (_view == View::Metar || _view == View::Map)) {   // edges step stations
    _sel = (x < third) ? (_sel - 1 + n) % n : (_sel + 1) % n;
    _needClear = _dirty = true;
  }
}

bool PageAviation::autoAdvance(App&) {
  bool cycled = false;
  auto nextView = [&]() {
    bool wasHazards = (_view == View::Hazards);
    _view = _view == View::Map ? View::Metar : _view == View::Metar ? View::Sounding
          : _view == View::Sounding ? View::Hazards : View::Map;
    _tourN = 0; _sel = 0;
    if (wasHazards) cycled = true;         // Hazards -> Map = full cycle
  };
  int n = (int)_wx.stations().size();
  if ((_view == View::Metar || _view == View::Map) && n > 0) {
    _sel = (_sel + 1) % n;
    if (++_tourN >= n) nextView();         // toured all stations -> next view
  } else {
    nextView();                            // Sounding/Hazards (no items): one dwell, next view
  }
  _needClear = _dirty = true;
  return cycled;
}

void PageAviation::tick(App& app, uint32_t nowMs) {
  if (!_dirty && nowMs - _lastDraw < 5000) return;
  _dirty = false; _lastDraw = nowMs;
  draw(app);
}

void PageAviation::draw(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  if (_needClear) { g.fillRect(0, cy0, cw, ch, gTheme.bg); _needClear = false; }
  if (_view == View::Metar)         drawMetar(app);
  else if (_view == View::Map)      drawMap(app);
  else if (_view == View::Sounding) drawSounding(app);
  else                              drawHazards(app);
}

void PageAviation::drawMetar(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  const auto& list = _wx.stations();
  if (list.empty()) {
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(!_loc.active().valid ? "no location"
               : _wx.status() == ProviderStatus::Error ? "AWC unavailable" : "loading METARs...",
                 cw / 2, cy0 + ch / 2);
    return;
  }
  if (_sel >= (int)list.size()) _sel = 0;
  const Metar& m = list[_sel];
  const int maxc = (cw - 10) / 6;
  int x0 = 6, y = cy0 + 4;

  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.setTextSize(2); g.drawString(m.icao, x0, y);
  g.setTextDatum(textdatum_t::top_right);
  g.setTextColor(catColor(m.cat), gTheme.bg); g.drawString(m.cat, cw - 6, y + 2);
  g.setTextDatum(textdatum_t::top_left); y += 20;
  g.setTextSize(1);
  auto line = [&](const String& s, Color c) { g.setTextColor(c, gTheme.bg); g.drawString(padRight(s, maxc), x0, y); y += 12; };

  // Observation time: Zulu + local, e.g. 160354Z / 8:54PM.
  if (m.obsTime) {
    struct tm gu, lo; time_t t = m.obsTime; gmtime_r(&t, &gu); localtime_r(&t, &lo);
    char z[10], l[10]; strftime(z, sizeof(z), "%d%H%MZ", &gu); strftime(l, sizeof(l), "%I:%M%p", &lo);
    line(String(z) + " / " + l, gTheme.dim);
  }
  line(m.name.substring(0, maxc) + "  " + (int)round(m.distNm) + "nm  [tap mid: sounding]", gTheme.dim);
  if (m.wspd >= 0) line(m.wspd == 0 ? String("wind calm")
        : String("wind ") + (m.wdir < 0 ? String("VRB") : String(m.wdir)) + "\xF7 @ " + m.wspd
          + " kt (" + (int)round(m.wspd * 1.15078f) + " mph)", gTheme.fg);
  if (m.visSm >= 0)   line(String("vis ") + String(m.visSm, 0) + " sm", gTheme.fg);
  line(m.ceilingFt >= 0 ? String("ceiling ") + m.ceilingFt + " ft" : String("ceiling: none"), gTheme.fg);
  if (m.tempC > -999) line(String("temp ") + m.tempC + " (" + (m.tempC * 9 / 5 + 32) + "F)  dewpt "
                           + m.dewpC + " (" + (m.dewpC * 9 / 5 + 32) + "F)", gTheme.fg);
  if (m.altimHpa > 0) line(String("QNH ") + m.altimHpa + " hPa (" + String(m.altimHpa / 33.8639f, 2) + " inHg)", gTheme.fg);
  if (m.wx.length())  line(String("wx ") + m.wx, gTheme.warn);
  line(String(_sel + 1) + "/" + list.size(), gTheme.dim);
  y += 3; g.setTextColor(gTheme.dim, gTheme.bg); g.drawString("METAR", x0, y); y += 11;
  y = drawWrapped(g, m.raw, x0, y, maxc, 3, gTheme.fg);
  if (m.taf.length() && y < cy0 + ch - 24) {
    y += 2; g.setTextColor(gTheme.dim, gTheme.bg); g.drawString("TAF", x0, y); y += 11;
    drawWrapped(g, m.taf, x0, y, maxc, (cy0 + ch - y) / 11, gTheme.dim);
  }
}

void PageAviation::drawMap(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  const auto& list = _wx.stations();
  double z = kMapZoom[_mapZoom];
  const double latR = 0.95 * z, lonR = 1.25 * z;    // half-ranges (scaled by zoom)

  // Zoom badge (top-left, tappable) + hint.
  int rNm = (int)round(latR * 60.0);                // 1 deg lat ~ 60 nm
  g.fillRect(2, cy0, 46, 13, gTheme.grid);
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.fg, gTheme.grid);
  g.drawString(String("~") + rNm + "nm", 5, cy0 + 2);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString("tap:zoom  mid:metar", 52, cy0 + 2);

  if (list.empty() || !_loc.active().valid) {
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(_loc.active().valid ? "no stations" : "no location", 4, cy0 + ch / 2);
    return;
  }

  double olat = _loc.active().lat, olon = _loc.active().lon;
  int mapY = cy0 + 14, mapH = ch - 14 - 38;        // bottom block: detail line + raw METAR (2 lines)
  int cx = cw / 2, cyc = mapY + mapH / 2;
  auto SX = [&](double lon) { return cx + (int)((lon - olon) / lonR * (cw / 2 - 8)); };
  auto SY = [&](double lat) { return cyc - (int)((lat - olat) / latR * (mapH / 2 - 8)); };

  g.drawRect(2, mapY, cw - 4, mapH, gTheme.grid);
  g.drawFastHLine(2, cyc, cw - 4, gTheme.grid);
  g.drawFastVLine(cx, mapY, mapH, gTheme.grid);
  g.drawFastHLine(cx - 3, cyc, 7, gTheme.ok);       // observer
  g.drawFastVLine(cx, cyc - 3, 7, gTheme.ok);

  for (int i = 0; i < (int)list.size(); ++i) {
    const Metar& s = list[i];
    int x = SX(s.lon), y = SY(s.lat);
    if (x < 4 || x > cw - 4 || y < mapY + 2 || y > mapY + mapH - 2) continue;
    Color c = catColor(s.cat);
    if (s.wspd > 0 && s.wdir >= 0) {                 // wind vector toward the FROM dir
      int L = 4 + min(s.wspd, 30) / 2;
      g.drawLine(x, y, x + (int)(L * sin(s.wdir * D2R)), y - (int)(L * cos(s.wdir * D2R)), c);
    }
    int rad = (i == _sel) ? 4 : 2;
    g.fillCircle(x, y, rad, c);
    if (i == _sel) g.drawCircle(x, y, rad + 2, gTheme.fg);
  }

  // Legend (top-right).
  g.setTextDatum(textdatum_t::top_right);
  g.setTextColor(gTheme.ok, gTheme.bg);  g.drawString("VFR", cw - 4, cy0 + 2);
  g.setTextColor(gTheme.accent, gTheme.bg); g.drawString("MVFR", cw - 30, cy0 + 2);
  g.setTextColor(gTheme.warn, gTheme.bg); g.drawString("I/LIFR", cw - 64, cy0 + 2);

  // Selected detail: ICAO + category + decoded summary, then the full raw METAR.
  if (_sel >= 0 && _sel < (int)list.size()) {
    const Metar& s = list[_sel];
    int by = mapY + mapH + 2;
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(catColor(s.cat), gTheme.bg);
    g.drawString(s.icao + " " + s.cat, 4, by);
    g.setTextColor(gTheme.fg, gTheme.bg);
    char b[64];
    snprintf(b, sizeof(b), "vis%.0f cig%d %d/%dC w%d@%d", s.visSm, s.ceilingFt,
             s.tempC, s.dewpC, s.wdir < 0 ? 0 : s.wdir, s.wspd < 0 ? 0 : s.wspd);
    g.setTextDatum(textdatum_t::top_right);
    g.drawString(b, cw - 4, by);
    if (s.raw.length())                            // full raw METAR, wrapped (2 lines)
      drawWrapped(g, s.raw, 4, by + 11, (cw - 8) / 6, 2, gTheme.dim);
  }
}

void PageAviation::drawSounding(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  const auto& lv = _snd.levels();
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString("Sounding (model)  [tap mid: hazards]", 6, cy0 + 2);
  if (lv.size() < 3) {
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(_snd.status() == ProviderStatus::Error ? "sounding unavailable" : "loading sounding...", 6, cy0 + ch / 2);
    return;
  }

  // Plot box: x = temp -40..40, y = alt 0..12 km.
  const int gx = 24, gy = cy0 + 16, gw = cw - 90, gh = ch - 24;
  const float Tmin = -40, Tmax = 40, Amax = 12000;
  auto px = [&](float T) { return gx + (int)((T - Tmin) / (Tmax - Tmin) * gw); };
  auto py = [&](float a) { return gy + gh - (int)(fminf(a, Amax) / Amax * gh); };
  g.drawRect(gx, gy, gw, gh, gTheme.grid);
  g.setTextColor(gTheme.dim, gTheme.bg);
  if (px(0) > gx && px(0) < gx + gw) g.drawFastVLine(px(0), gy, gh, gTheme.grid);  // 0 C
  for (int km = 3; km <= 12; km += 3) { int yy = py(km * 1000); g.drawFastHLine(gx, yy, gw, gTheme.grid); g.drawString(String(km), 4, yy - 3); }

  int px0t = -1, py0t = 0, px0d = -1, py0d = 0;
  for (const auto& l : lv) {
    if (l.altM > Amax) break;
    int xt = px(l.tempC), yt = py(l.altM);
    if (px0t >= 0) g.drawLine(px0t, py0t, xt, yt, gTheme.warn);
    px0t = xt; py0t = yt;
    if (l.dewpC > -900) { int xd = px(l.dewpC), yd = py(l.altM); if (px0d >= 0) g.drawLine(px0d, py0d, xd, yd, gTheme.accent); px0d = xd; py0d = yd; }
  }

  // Freezing level.
  if (_snd.freezingLevelM() > 0) {
    int yf = py(_snd.freezingLevelM());
    g.drawFastHLine(gx, yf, gw, gTheme.ok);
    g.setTextColor(gTheme.ok, gTheme.bg);
    g.setTextDatum(textdatum_t::bottom_right);
    g.drawString(String("FZL ") + (int)(_snd.freezingLevelM() * 3.281f) + "ft", cw - 2, yf);
    g.setTextDatum(textdatum_t::top_left);
  }

  // Winds aloft (nearest levels to 1/3/6/9 km).
  int wy = gy + 2; const int targets[] = {1000, 3000, 6000, 9000};
  g.setTextColor(gTheme.fg, gTheme.bg);
  for (int t : targets) {
    const SoundingLevel* best = nullptr; float bd = 1e9;
    for (const auto& l : lv) if (l.wspd >= 0) { float d = fabsf(l.altM - t); if (d < bd) { bd = d; best = &l; } }
    if (best) { char b[24]; snprintf(b, sizeof(b), "%dk %03d@%d", t / 1000, best->wdir, best->wspd); g.drawString(b, cw - 60, wy); wy += 11; }
  }
}

void PageAviation::drawHazards(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  const int maxc = (cw - 10) / 6;
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString("Hazards  [tap mid: map]", 6, cy0 + 2);
  const auto& hz = _haz.hazards();
  int y = cy0 + 16;
  if (hz.empty()) {
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString("no AIRMET/SIGMET/PIREP nearby", 6, cy0 + ch / 2);
    return;
  }
  for (const auto& h : hz) {
    if (y > cy0 + ch - 12) break;
    y = drawWrapped(g, (h.pirep ? "PIREP " : "") + h.text, 6, y, maxc, 3, h.pirep ? gTheme.dim : gTheme.warn);
    y += 3;
  }
}
