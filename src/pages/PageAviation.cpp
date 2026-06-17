#include "PageAviation.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../providers/AviationWxProvider.h"
#include "../providers/SoundingProvider.h"
#include "../providers/HazardProvider.h"
#include "../providers/WeatherProvider.h"
#include "../providers/PressureMapProvider.h"
#include "../assets/Coastline.h"
#include "../services/LocationService.h"
#include <math.h>
#include <functional>

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

void PageAviation::focusSpeci() {
  const auto& st = _wx.stations();
  for (int i = 0; i < (int)st.size(); ++i)
    if (st[i].raw.startsWith("SPECI")) { _sel = i; _view = View::Metar; _needClear = _dirty = true; return; }
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
    _view = _view == View::Map ? View::Metar : _view == View::Metar ? View::Taf
          : _view == View::Taf ? View::Sounding : _view == View::Sounding ? View::Hazards
          : _view == View::Hazards ? View::Trends : _view == View::Trends ? View::Pressure : View::Map;
    _needClear = _dirty = true; return;
  }
  if (_view == View::Map && x < 50 && y < 16) {     // top-left badge: cycle map zoom
    _mapZoom = (_mapZoom + 1) % kMapZoomN;
    _needClear = _dirty = true; return;
  }
  if (_view == View::Pressure && x < 60 && y < 16) { // top-left badge: pressure<->cloud
    _presCloud = !_presCloud; _needClear = _dirty = true; return;
  }
  int n = (int)_wx.stations().size();
  if (n && (_view == View::Metar || _view == View::Map || _view == View::Taf)) {   // edges step stations
    _sel = (x < third) ? (_sel - 1 + n) % n : (_sel + 1) % n;
    _needClear = _dirty = true;
  }
}

bool PageAviation::autoAdvance(App&) {
  bool cycled = false;
  auto nextView = [&]() {
    bool wasLast = (_view == View::Pressure);
    _view = _view == View::Map ? View::Metar : _view == View::Metar ? View::Taf
          : _view == View::Taf ? View::Sounding : _view == View::Sounding ? View::Hazards
          : _view == View::Hazards ? View::Trends : _view == View::Trends ? View::Pressure : View::Map;
    _tourN = 0; _sel = 0;
    if (wasLast) cycled = true;            // Pressure -> Map = full cycle
  };
  int n = (int)_wx.stations().size();
  if ((_view == View::Metar || _view == View::Map || _view == View::Taf) && n > 0) {
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
  else if (_view == View::Taf)      drawTaf(app);
  else if (_view == View::Sounding) drawSounding(app);
  else if (_view == View::Hazards)  drawHazards(app);
  else if (_view == View::Trends)   drawTrends(app);
  else                              drawPressure(app);
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
  line(m.name.substring(0, maxc) + "  " + (int)round(m.distNm) + "nm  [tap mid: taf]", gTheme.dim);
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
    // Identifier near the dot (drop the US 'K' prefix to keep it short).
    String id = s.icao;
    if (id.length() == 4 && id[0] == 'K') id = id.substring(1);
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextColor(i == _sel ? gTheme.fg : gTheme.dim, gTheme.bg);
    g.drawString(id, x + rad + 2, y);
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

// --- compact TAF token decode ---
static String tafWind(const String& t) {
  if (!t.endsWith("KT") || t.length() < 7) return "";
  if (t == "00000KT") return "calm";
  String dir = t.substring(0, 3);
  if (dir != "VRB") for (int k = 0; k < 3; ++k) if (!isdigit(dir[k])) return "";
  int gi = t.indexOf('G');
  String spd = t.substring(3, gi > 0 ? gi : t.length() - 2);
  String s = dir + "@" + String(spd.toInt());
  if (gi > 0) s += "g" + String(t.substring(gi + 1, t.length() - 2).toInt());
  return s + "kt";
}
static String tafVis(const String& t) {
  if (!t.endsWith("SM") || t.length() < 3) return "";
  String v = t.substring(0, t.length() - 2), pre;
  if (v.startsWith("P")) { pre = ">"; v = v.substring(1); }
  else if (v.startsWith("M")) { pre = "<"; v = v.substring(1); }
  if (!v.length() || !(isdigit(v[0]))) return "";
  return pre + v + "sm";
}
static String tafTok(const String& t) {                 // wind/vis decoded; rest passed through
  String w = tafWind(t); if (w.length()) return w;
  String v = tafVis(t);  if (v.length()) return v;
  return t;                                              // FEW/SCT/BKN/OVC, wx, CAVOK, SKC...
}

void PageAviation::drawTaf(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  const auto& list = _wx.stations();
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  if (list.empty() || _sel >= (int)list.size()) {
    g.drawString("TAF  [tap mid: sounding]", 6, cy0 + 2);
    g.setTextColor(gTheme.dim, gTheme.bg); g.drawString("no station", 6, cy0 + ch / 2); return;
  }
  const Metar& m = list[_sel];
  g.drawString(m.icao + " TAF  [tap mid: sounding]", 6, cy0 + 2);
  if (!m.taf.length()) { g.setTextColor(gTheme.dim, gTheme.bg); g.drawString("no TAF for this field", 6, cy0 + ch / 2); return; }

  const String& t = m.taf; int i = 0, n = t.length();
  int y = cy0 + 16; const int maxc = (cw - 10) / 6;
  String label = "now", parts, tok; bool gotIcao = false;
  auto nextTok = [&](String& o) -> bool { while (i < n && t[i] == ' ') i++; if (i >= n) return false; int s = i; while (i < n && t[i] != ' ') i++; o = t.substring(s, i); return true; };
  auto rng = [&](const String& r) -> String { return (r.length() == 9 && r[4] == '/') ? r.substring(2, 4) + "-" + r.substring(7, 9) : r; };
  auto emit = [&]() {
    if (!parts.length() || y > cy0 + ch - 12) { parts = ""; return; }
    g.setTextColor(gTheme.warn, gTheme.bg); g.drawString(padRight(label, 9).substring(0, 9), 6, y);
    g.setTextColor(gTheme.fg, gTheme.bg);   g.drawString(parts.substring(0, maxc - 9), 6 + 9 * 6, y);
    y += 12; parts = "";
  };
  bool haveValid = false;
  while (nextTok(tok)) {
    if (tok == "TAF" || tok == "AMD" || tok == "COR") continue;
    if (!gotIcao && tok.length() == 4) { gotIcao = true; continue; }     // ICAO
    if (tok.endsWith("Z") && tok.length() >= 6) continue;                // issue time
    if (!haveValid && tok.length() == 9 && tok[4] == '/') { haveValid = true; continue; }  // overall valid
    if (tok.startsWith("FM") && tok.length() == 8) { emit(); label = "FM" + tok.substring(2, 4) + "z"; continue; }
    if (tok == "BECMG") { emit(); String r; label = nextTok(r) ? "bcmg " + rng(r) : "becmg"; continue; }
    if (tok == "TEMPO") { emit(); String r; label = nextTok(r) ? "tmpo " + rng(r) : "tempo"; continue; }
    if (tok.startsWith("PROB")) { emit(); String r; label = "p" + tok.substring(4); if (nextTok(r)) label += " " + rng(r); continue; }
    parts += tafTok(tok) + " ";
  }
  emit();
  if (y == cy0 + 16) { g.setTextColor(gTheme.dim, gTheme.bg); g.drawString("(could not parse)", 6, y); }
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

  // Plot box: x = temp -40..40 C, y = altitude 0..40 kft (aviation-native units).
  const float M2FT = 3.28084f;
  const int gx = 24, gy = cy0 + 16, gw = cw - 90, gh = ch - 40;   // bottom rows: legend + analysis
  const float Tmin = -40, Tmax = 40, AmaxFt = 40000;
  auto px  = [&](float T)  { return gx + (int)((T - Tmin) / (Tmax - Tmin) * gw); };
  auto pyf = [&](float ft) { return gy + gh - (int)(fminf(ft, AmaxFt) / AmaxFt * gh); };
  auto pym = [&](float m)  { return pyf(m * M2FT); };
  auto dash = [&](int x0, int y0, int x1, int y1, Color c) {     // dashed straight line
    int n = max(abs(x1 - x0), abs(y1 - y0)) / 4; if (n < 1) n = 1;
    for (int k = 0; k < n; k += 2) {
      float f0 = (float)k / n, f1 = (float)(k + 1) / n;
      g.drawLine(x0 + (int)((x1 - x0) * f0), y0 + (int)((y1 - y0) * f0),
                 x0 + (int)((x1 - x0) * f1), y0 + (int)((y1 - y0) * f1), c);
    }
  };
  g.drawRect(gx, gy, gw, gh, gTheme.grid);
  g.setTextColor(gTheme.dim, gTheme.bg);
  if (px(0) > gx && px(0) < gx + gw) g.drawFastVLine(px(0), gy, gh, gTheme.grid);  // 0 C
  for (int kft = 10; kft <= 30; kft += 10) { int yy = pyf(kft * 1000); g.drawFastHLine(gx, yy, gw, gTheme.grid); g.drawString(String(kft), 4, yy - 3); }

  // Dashed dry-adiabat parcel from the surface (where it crosses temp = top of lift).
  if (!lv.empty()) {
    float a0 = lv.front().altM, t0 = lv.front().tempC, topM = AmaxFt / M2FT;
    dash(px(t0), pym(a0), px(t0 - 9.8f * (topM - a0) / 1000.0f), pyf(AmaxFt), gTheme.dim);
  }

  int px0t = -1, py0t = 0, px0d = -1, py0d = 0;
  for (const auto& l : lv) {
    if (l.altM * M2FT > AmaxFt) break;
    int xt = px(l.tempC), yt = pym(l.altM);
    if (px0t >= 0) g.drawLine(px0t, py0t, xt, yt, gTheme.warn);
    px0t = xt; py0t = yt;
    if (l.dewpC > -900) { int xd = px(l.dewpC), yd = pym(l.altM); if (px0d >= 0) g.drawLine(px0d, py0d, xd, yd, gTheme.accent); px0d = xd; py0d = yd; }
  }

  // Freezing level.
  if (_snd.freezingLevelM() > 0) {
    int yf = pym(_snd.freezingLevelM());
    g.drawFastHLine(gx, yf, gw, gTheme.ok);
    g.setTextColor(gTheme.ok, gTheme.bg);
    g.setTextDatum(textdatum_t::bottom_right);
    g.drawString(String("FZL ") + (int)(_snd.freezingLevelM() * M2FT) + "ft", cw - 2, yf);
    g.setTextDatum(textdatum_t::top_left);
  }

  // Winds aloft (nearest levels to 3/10/20/30 kft), each at its own altitude's y.
  const int targets[] = {3000, 10000, 20000, 30000};
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.setTextDatum(textdatum_t::middle_left);
  for (int t : targets) {
    float tgtM = t / M2FT; const SoundingLevel* best = nullptr; float bd = 1e9;
    for (const auto& l : lv) if (l.wspd >= 0) { float d = fabsf(l.altM - tgtM); if (d < bd) { bd = d; best = &l; } }
    if (best) {
      char b[24]; snprintf(b, sizeof(b), "%dk %03d@%dkt", t / 1000, best->wdir, best->wspd);
      g.setTextDatum(textdatum_t::middle_right);
      g.drawString(b, cw - 2, pyf(best->altM * M2FT));    // altitude label + wind, at that altitude's y
    }
  }
  g.setTextDatum(textdatum_t::top_left);

  // Legend.
  int lgy = cy0 + ch - 22, lx = 4;
  auto key = [&](Color c, const String& s) {
    g.fillCircle(lx + 2, lgy + 3, 2, c); lx += 8;
    g.setTextDatum(textdatum_t::top_left); g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(s, lx, lgy); lx += (int)s.length() * 6 + 6;
  };
  key(gTheme.warn, "temp"); key(gTheme.accent, "dewpt"); key(gTheme.ok, "FZL");
  key(gTheme.dim, "parcel"); g.drawString("x:C y:kft", lx, lgy);

  // Analysis line (model estimates): stability / cloud base / lift top / inversion.
  static const char* kStab[] = {"stable", "neutral", "unstable", "strong lift"};
  auto kft = [](float ft) { return String(ft / 1000.0f, 1) + "k"; };
  String a;
  if (_snd.stability() >= 0) a = kStab[_snd.stability()];
  if (_snd.cloudBaseFt() > 0)  a += (a.length() ? " \xB7 " : "") + String("base ") + kft(_snd.cloudBaseFt());
  float surfFt = lv.front().altM * M2FT;
  if (_snd.thermalTopFt() > surfFt + 200) a += " \xB7 top " + kft(_snd.thermalTopFt());
  if (_snd.inversionFt() > 0)  a += " \xB7 inv " + kft(_snd.inversionFt());
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.drawString(a.length() ? a : String("(analysis n/a)"), 4, cy0 + ch - 10);
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

// Area weather trends from the Open-Meteo hourly series (one request, shared with
// the Agenda Sky Window): temp / dewpoint / cloud / pressure sparklines over the
// next ~24h, plus a rule-based conclusion. "Area" = the observer location (Open-
// Meteo is point-based); per-airport current values live in the METAR card.
void PageAviation::drawTrends(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(1);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.drawString("Area trends 24h  [tap mid: map]", 4, cy0 + 2);

  const auto& temp = _wxo.tempSeries();
  const auto& dewp = _wxo.dewpSeries();
  const auto& cloud = _wxo.cloudSeries();
  const auto& pres = _wxo.presSeries();
  time_t base = _wxo.baseTime();
  if (base == 0 || temp.size() < 3 || pres.size() < 3) {
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(_wxo.status() == ProviderStatus::Error ? "weather unavailable" : "loading trends...", 4, cy0 + ch / 2);
    return;
  }
  const int SENT = -32768;
  auto gT = [&](int i) { return (i >= 0 && i < (int)temp.size())  ? (int)temp[i]  : SENT; };
  auto gD = [&](int i) { return (i >= 0 && i < (int)dewp.size())  ? (int)dewp[i]  : SENT; };
  auto gC = [&](int i) { return (i >= 0 && i < (int)cloud.size()) ? (int)cloud[i] : SENT; };
  auto gP = [&](int i) { return (i >= 0 && i < (int)pres.size())  ? (int)pres[i]  : SENT; };

  int now = (int)((time(nullptr) - base) / 3600); if (now < 0) now = 0;
  const int span = 24;
  const int gx = 62, gw = cw - gx - 6;
  const int top = cy0 + 15, rowH = (ch - 15 - 15) / 4;

  auto sparkRow = [&](int r, const char* name, std::function<int(int)> get, const String& cur, const String& cur2, Color c) {
    int y0 = top + r * rowH;
    int mn = 32767, mx = -32768;
    for (int i = now; i <= now + span; ++i) { int v = get(i); if (v == SENT) continue; if (v < mn) mn = v; if (v > mx) mx = v; }
    if (mn > mx) return;
    if (mn == mx) mx = mn + 1;
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(gTheme.dim, gTheme.bg); g.drawString(name, 4, y0);
    g.setTextColor(c, gTheme.bg);          g.drawString(cur, 4, y0 + 10);
    if (cur2.length()) { g.setTextColor(gTheme.dim, gTheme.bg); g.drawString(cur2, 4, y0 + 20); }  // imperial
    int ph = rowH - 6, py = y0 + 2, px = -1, pv = 0;
    g.drawFastVLine(gx, py, ph, gTheme.grid);          // "now" baseline
    for (int i = now; i <= now + span; ++i) {
      int v = get(i); if (v == SENT) { px = -1; continue; }
      int x = gx + (int)((long)(i - now) * gw / span);
      int yy = py + ph - (int)((long)(v - mn) * ph / (mx - mn));
      if (px >= 0) g.drawLine(px, pv, x, yy, c);
      px = x; pv = yy;
    }
  };

  char cv[16], cv2[16];
  int tc = gT(now) == SENT ? 0 : gT(now), dc = gD(now) == SENT ? 0 : gD(now), pp = gP(now) == SENT ? 0 : gP(now);
  snprintf(cv, sizeof(cv), "%d\xF7" "C", tc); snprintf(cv2, sizeof(cv2), "%d\xF7" "F", tc * 9 / 5 + 32);
  sparkRow(0, "temp", gT, cv, cv2, gTheme.warn);
  snprintf(cv, sizeof(cv), "%d\xF7" "C", dc); snprintf(cv2, sizeof(cv2), "%d\xF7" "F", dc * 9 / 5 + 32);
  sparkRow(1, "dewpt", gD, cv, cv2, gTheme.accent);
  snprintf(cv, sizeof(cv), "%d%%", gC(now) == SENT ? 0 : gC(now));
  sparkRow(2, "cloud", gC, cv, "", gTheme.fg);
  snprintf(cv, sizeof(cv), "%dhPa", pp); snprintf(cv2, sizeof(cv2), "%.2fin", pp * 0.02953f);
  sparkRow(3, "press", gP, cv, cv2, gTheme.ok);

  // Conclusion over the displayed window (now -> end), so it matches the graph.
  int Pe = SENT, Ce = SENT;
  for (int i = now + span; i > now; --i) { if (gP(i) != SENT) { Pe = gP(i); break; } }
  for (int i = now + span; i > now; --i) { if (gC(i) != SENT) { Ce = gC(i); break; } }
  int dP = (Pe != SENT && gP(now) != SENT) ? Pe - gP(now) : 0;
  int dC = (Ce != SENT && gC(now) != SENT) ? Ce - gC(now) : 0;
  int spread = (gT(now) != SENT && gD(now) != SENT) ? gT(now) - gD(now) : 99;
  String concl = dP <= -2 ? "pressure falling - unsettled"
               : dP >= 2  ? "pressure rising - improving" : "pressure steady";
  if (spread <= 2)      concl += "; fog/low-cld risk";
  else if (dC >= 25)    concl += "; clouding over";
  else if (dC <= -25)   concl += "; clearing";
  g.setTextDatum(textdatum_t::bottom_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString(concl.substring(0, (cw - 8) / 6), 4, cy0 + ch - 2);
}

// Makeshift synoptic map from major-airport METARs: each station plotted on a
// region coastline, coloured by sea-level pressure (blue high / red low) with the
// max/min flagged H/L, or by cloud cover (top-left badge toggles). Not a real WPC
// frontal analysis, but a recognisable high/low pattern from a feed we already use.
void PageAviation::drawPressure(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  const auto& pts = _pmap.points();
  bool world = _pmap.worldwide();

  g.setTextDatum(textdatum_t::top_left); g.setTextSize(1);
  g.fillRect(2, cy0 + 2, 52, 12, gTheme.grid);                 // mode badge
  g.setTextColor(gTheme.fg, gTheme.grid); g.drawString(_presCloud ? "cloud" : "hPa", 6, cy0 + 3);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.drawString(String(world ? "World " : "US ") + (_presCloud ? "cloud" : "pressure") + "  [tap mid: metar]", 60, cy0 + 3);

  if (pts.empty()) {
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(_pmap.status() == ProviderStatus::Error ? "pressure feed down" : "loading...", 6, cy0 + ch / 2);
    return;
  }

  double w0 = world ? -180 : -126, w1 = world ? 180 : -66, a0 = world ? -60 : 24, a1 = world ? 78 : 50;
  int mx = 2, my = cy0 + 16, mw = cw - 4, mh = ch - 16 - 12;
  auto SX = [&](double lon) { return mx + (int)((lon - w0) / (w1 - w0) * mw); };
  auto SY = [&](double lat) { return my + (int)((a1 - lat) / (a1 - a0) * mh); };
  g.drawRect(mx, my, mw, mh, gTheme.grid);
  for (int i = 1; i < kCoastlineCount; ++i) {                  // coastline within the bbox
    const CoastPt& a = kCoastline[i - 1]; const CoastPt& b = kCoastline[i];
    if (a.lon == 999 || b.lon == 999 || abs(a.lon - b.lon) > 180) continue;
    if (a.lon < w0 || a.lon > w1 || b.lon < w0 || b.lon > w1 || a.lat < a0 || a.lat > a1 || b.lat < a0 || b.lat > a1) continue;
    g.drawLine(SX(a.lon), SY(a.lat), SX(b.lon), SY(b.lat), gTheme.dim);
  }

  int hi = -1, lo = -1, hv = -9999, lv = 9999;
  for (size_t i = 0; i < pts.size(); ++i) {
    int p = pts[i].hpa; if (p < 0) continue;
    if (p > hv) { hv = p; hi = (int)i; }
    if (p < lv) { lv = p; lo = (int)i; }
  }
  for (size_t i = 0; i < pts.size(); ++i) {
    const PressurePt& p = pts[i];
    int x = SX(p.lon), y = SY(p.lat);
    if (x < mx || x > mx + mw || y < my || y > my + mh) continue;
    Color c = _presCloud ? (p.cloud >= 70 ? gTheme.dim : p.cloud >= 30 ? gTheme.accent : gTheme.ok)
                         : (p.hpa >= 1019 ? gTheme.accent : p.hpa <= 1009 ? gTheme.warn : gTheme.fg);
    g.fillCircle(x, y, 2, c);
    char b[8]; if (_presCloud) snprintf(b, sizeof(b), "%d", p.cloud); else snprintf(b, sizeof(b), "%02d", p.hpa % 100);
    g.setTextDatum(textdatum_t::middle_left); g.setTextColor(c, gTheme.bg);
    g.drawString(b, x + 3, y);
  }
  if (!_presCloud) {                                           // H / L markers
    if (hi >= 0) { g.setTextDatum(textdatum_t::middle_center); g.setTextColor(gTheme.accent, gTheme.bg); g.drawString("H", SX(pts[hi].lon), SY(pts[hi].lat) - 8); }
    if (lo >= 0) { g.setTextDatum(textdatum_t::middle_center); g.setTextColor(gTheme.warn,   gTheme.bg); g.drawString("L", SX(pts[lo].lon), SY(pts[lo].lat) - 8); }
  }
  g.setTextDatum(textdatum_t::bottom_left); g.setTextColor(gTheme.dim, gTheme.bg);
  if (!_presCloud) g.drawString(String("H ") + hv + "  L " + lv + " hPa  blue=high red=low", 4, cy0 + ch - 1);
  else             g.drawString("cloud %  green clear / blue part / dim overcast", 4, cy0 + ch - 1);
}
