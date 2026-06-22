#include "PageAgenda.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../services/TimeService.h"
#include "../services/LocationService.h"
#include "../providers/WeatherProvider.h"
#include "../providers/TleProvider.h"
#include "../providers/LaunchProvider.h"
#include "../services/Settings.h"
#include "../astro/Sun.h"
#include "../astro/Time.h"
#include "../astro/Coords.h"
#include "../astro/SolarSystem.h"
#include "../assets/MeteorShowers.h"
#include "../assets/StarCatalog.h"
#include <string.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <time.h>

static Color darknessShade(float sunAlt) {
  if (gTheme.mono) {                                // red dark-adapt: red-channel ramp
    if (sunAlt > -0.5) return rgb565(48, 8, 4);     // day
    if (sunAlt > -6)   return rgb565(32, 5, 2);     // civil
    if (sunAlt > -12)  return rgb565(20, 3, 1);     // nautical
    if (sunAlt > -18)  return rgb565(12, 2, 0);     // astronomical
    return rgb565(6, 1, 0);                         // night
  }
  if (sunAlt > -0.5) return rgb565(46, 52, 66);     // day
  if (sunAlt > -6)   return rgb565(32, 36, 50);     // civil
  if (sunAlt > -12)  return rgb565(20, 22, 34);     // nautical
  if (sunAlt > -18)  return rgb565(12, 13, 22);     // astronomical
  return rgb565(6, 7, 12);                          // night
}

static bool brightSat(const String& n) {
  return n.startsWith("ISS") || n.startsWith("CSS") || n.indexOf("TIANGONG") >= 0 || n.startsWith("HST");
}
static bool radioSat(const String& n) {
  static const char* p[] = {"ISS", "SO-", "AO-", "PO-", "RS-", "FO-", "CAS-", "XW-", "JO-", "LO-", "TO-", "TEVEL", "HADES", "MESAT", "UVSQ"};
  for (auto x : p) if (n.startsWith(x)) return true;
  return false;
}

void PageAgenda::recompute() {
  if (!_time.synced() || !_loc.active().valid) return;
  double lat = _loc.active().lat, lon = _loc.active().lon;
  _base = time(nullptr);

  for (int h = 0; h < kHours; ++h) {
    time_t t = _base + (time_t)h * 3600;
    double jd = astro::julianDate(t);
    _sunAlt[h] = (float)astro::sunAltitudeDeg(jd, lat, lon);
    _cloud[h]  = (int8_t)_wx.cloudCoverAt(t);
    _moonUp[h] = astro::planetState(astro::Planet::Moon, jd, lat, lon).above;
  }

  // Events: next pass per watchlisted bird + upcoming launches (next 24 h).
  _events.clear();
  time_t now = _base, horizon = _base + (time_t)kHours * 3600;
  if (!_tle.sats().empty()) {
    _eng.setObserver(lat, lon, 0);
    int minEl = (int)_settings.getInt("satMinEl", 10);
    JsonArray wl = _settings.doc()["watchlist"].as<JsonArray>();
    for (JsonVariant v : wl) {
      String pre = (const char*)(v | "");
      if (!pre.length()) continue;
      for (const auto& s : _tle.sats()) {
        if (!satNameMatches(s.name, pre)) continue;
        _eng.loadTle(s.name.c_str(), s.line1.c_str(), s.line2.c_str());
        time_t from = now - 1200; astro::SatPass p;     // catch an in-progress pass too
        for (int k = 0; k < 3; ++k) { p = _eng.nextPass(from, (double)minEl, 40); if (!p.valid || p.los > now) break; from = p.los + 60; }
        if (p.valid && p.los > now && p.aos < horizon) {
          bool dark = astro::sunAltitudeDeg(astro::julianDate(p.tca), lat, lon) < -6.0;
          bool vis = brightSat(s.name) && dark && _eng.observe(p.tca).sunlit;
          String lbl = s.name + " " + (int)round(p.maxElDeg) + (char)247;
          if (vis) lbl += " VIS";
          if (radioSat(s.name)) lbl += " RF";
          struct tm tl; time_t lt = p.los; localtime_r(&lt, &tl);   // append LOS clock
          char losb[12]; snprintf(losb, sizeof(losb), " >%02d:%02d", tl.tm_hour, tl.tm_min);
          lbl += losb;
          _events.push_back({ p.aos, lbl, 0, s.name });   // ref = sat name -> focus on jump
        }
        break;
      }
    }
  }
  for (const auto& l : _launch.launches())
    if (l.net && l.net > now && l.net < horizon)
      _events.push_back({ l.net, l.name, 1, l.name });   // ref = launch name -> focus on jump

  // Sun/Moon rise & set crossings (from the hourly sun-altitude / moon-up arrays).
  // Sun crossings are interpolated within the hour for a sub-hour time; the moon
  // up/down flip is reported at hour resolution.
  for (int h = 1; h < kHours; ++h) {
    if ((_sunAlt[h - 1] < 0) != (_sunAlt[h] < 0)) {
      float f = _sunAlt[h - 1] / (_sunAlt[h - 1] - _sunAlt[h]);     // 0..1 in the hour
      time_t t = _base + (time_t)((h - 1 + f) * 3600.0f);
      _events.push_back({ t, _sunAlt[h] >= 0 ? "Sunrise" : "Sunset", 2 });
    }
    if (_moonUp[h] != _moonUp[h - 1])
      _events.push_back({ _base + (time_t)h * 3600, _moonUp[h] ? "Moonrise" : "Moonset", 2 });
  }
  std::sort(_events.begin(), _events.end(), [](const Event& a, const Event& b) { return a.t < b.t; });

  // Verdict: first window that is dark (sun < -12) and mostly clear (< 35%).
  _verdict = "";
  for (int h = 0; h < kHours; ++h) {
    if (_sunAlt[h] < -12 && _cloud[h] >= 0 && _cloud[h] < 35) {
      int end = h;
      while (end < kHours && _sunAlt[end] < -12 && _cloud[end] >= 0 && _cloud[end] < 35) ++end;
      struct tm a, b; time_t ta = _base + h * 3600, tb = _base + end * 3600;
      localtime_r(&ta, &a); localtime_r(&tb, &b);
      char buf[48]; snprintf(buf, sizeof(buf), "Clear & dark %02d:%02d-%02d:%02d", a.tm_hour, a.tm_min, b.tm_hour, b.tm_min);
      _verdict = buf; break;
    }
  }
  if (_verdict.length() == 0) _verdict = _wx.valid() ? "No clear dark window in 24h" : "(clouds unavailable)";
}

String PageAgenda::gridStatus() {
  if (_events.empty()) return String();
  time_t now = time(nullptr);
  for (const auto& e : _events) {                  // _events is sorted ascending by time
    if (e.t <= now) continue;
    long s = (long)e.t - (long)now;
    String lbl = e.label; if (lbl.length() > 8) lbl = lbl.substring(0, 8);
    return lbl + " " + (s >= 3600 ? String(s / 3600) + "h" : String(s / 60) + "m");
  }
  return String();
}

void PageAgenda::tick(App& app, uint32_t nowMs) {
  // Heavy recompute (pass prediction for the watchlist) only every 5 min or on a
  // location change — NOT on every provider publish (that starved the loop/touch).
  bool ready = _time.synced() && _loc.active().valid;
  if (ready && (!_computed || nowMs - _lastRecompute > 300000)) {
    recompute(); _lastRecompute = nowMs; _computed = true; _dirty = true;
  }
  if (!_dirty && nowMs - _lastDraw < 15000) return;   // cheap redraw, <=15s
  _dirty = false; _lastDraw = nowMs;
  _needsDraw = true;   // defer the actual draw to render() so the App can tile it into SRAM (off-PSRAM)
}

// Pure redraw (no state change) — the App calls this once per SRAM band, clipped, so it must be
// safe to run many times per frame. On non-tiled boards it's never used (tick draws directly).
void PageAgenda::render(App& app) {
  draw(app);
}

void PageAgenda::jumpToEvent(App& app, int i) {
  if (i < 0 || i >= (int)_events.size()) return;
  const char* tab = _events[i].kind == 1 ? "Launches"
                  : _events[i].kind == 2 ? "Solar System" : "Satellites";
  int idx = app.pageIndexByTitle(tab);
  if (idx >= 0) {
    if (_events[i].ref.length()) app.requestFocus(_events[i].ref);   // focus the exact entry
    app.setPage(idx);
  }
}

void PageAgenda::onTouch(App& app, int x, int y) {
  // Sky Window: tap a vertical event stripe -> jump to that event (lines at sx+sw*h/24).
  const int sxx = 2, sw = app.contentW() - 4, syRel = 16, sh = 64;
  if (y >= syRel && y <= syRel + sh) {
    int best = -1, bestd = 11;
    for (int i = 0; i < (int)_events.size(); ++i) {
      int hoff = (int)((_events[i].t - _base) / 3600);
      if (hoff < 0 || hoff >= kHours) continue;
      int lx = sxx + sw * hoff / kHours, d = x > lx ? x - lx : lx - x;
      if (d < bestd) { bestd = d; best = i; }
    }
    if (best >= 0) { jumpToEvent(app, best); return; }
  }
  // Upcoming list: tap a row -> jump to that event (offset by the scroll position).
  if (!_events.empty() && _listN > 0) {
    int row = (y + app.contentY() - _listY0) / 13;
    if (row >= 0 && row < _listN) jumpToEvent(app, _listScroll + row);
  }
}

void PageAgenda::onScroll(App&, int dy) {
  int total = (int)_events.size();
  if (dy < 0) { if (_listScroll + _listN < total) { _listScroll++; _dirty = true; } }   // up -> later
  else        { if (_listScroll > 0)              { _listScroll--; _dirty = true; } }    // down -> earlier
}

void PageAgenda::draw(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  g.fillRect(0, cy0, cw, ch, gTheme.bg);

  if (!_time.synced() || !_loc.active().valid) {
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(_time.synced() ? "no location" : "waiting for time sync...", cw / 2, cy0 + ch / 2);
    return;
  }

  // --- Sky Window timeline ---
  const int sx = 2, sw = cw - 4;
  const int sy = cy0 + 16, sh = 64;
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.fg, gTheme.bg);
  { struct tm tmh; time_t nb = _base; localtime_r(&nb, &tmh);   // context by time of day
    const char* ctx = (tmh.tm_hour >= 18 || tmh.tm_hour < 5) ? "Tonight"
                    : (tmh.tm_hour < 12) ? "Today" : "This evening";
    g.drawString(String(ctx) + "  -  Sky Window (24h)", sx, cy0 + 2); }

  for (int h = 0; h < kHours; ++h) {
    int x0 = sx + sw * h / kHours, x1 = sx + sw * (h + 1) / kHours;
    int w = x1 - x0;
    g.fillRect(x0, sy, w, sh, darknessShade(_sunAlt[h]));      // darkness band
    if (_cloud[h] >= 0) {                                       // cloud heat (top)
      int gch = 20 + _cloud[h] * 180 / 100;
      g.fillRect(x0, sy, w, 14, gTheme.mono ? rgb565(gch, gch / 4, 0) : rgb565(gch, gch, gch + 10));
    }
    if (_moonUp[h]) g.fillRect(x0, sy + sh - 4, w, 4, gTheme.mono ? rgb565(90, 22, 0) : rgb565(70, 60, 30)); // moon-up
  }
  g.drawRect(sx, sy, sw, sh, gTheme.grid);
  // Hour ticks: local time only, with a small tick marking the exact moment.
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.setTextDatum(textdatum_t::top_left);
  for (int hh = 0; hh <= 24; hh += 6) {
    int x = sx + sw * hh / kHours;
    g.drawFastVLine(x, sy, sh, gTheme.grid);
    if (hh < 24) {                                   // skip the right-edge tick (would clip)
      g.drawFastVLine(x, sy + sh + 1, 2, gTheme.fg); // exact-time tick below the strip
      time_t t = _base + (time_t)hh * 3600; struct tm tm; localtime_r(&t, &tm);
      char lt[8]; strftime(lt, sizeof(lt), "%H:%M", &tm);
      g.setTextColor(gTheme.dim, gTheme.bg);
      g.drawString(lt, x + 3, sy + sh + 1);
    }
  }
  // Event markers (accent=pass, warn=launch, ok=sun/moon): a vertical line at the true
  // time, plus a stacked label. Labels are placed earliest-first (leftmost) and nudged
  // right so neighbours that fall close together sit side by side instead of overlapping.
  auto evColor = [&](const Event& e) {
    return e.kind == 1 ? gTheme.warn : e.kind == 2 ? gTheme.ok : gTheme.accent; };
  std::vector<int> order;                            // in-window events, sorted by time
  for (int i = 0; i < (int)_events.size(); ++i) {
    int hoff = (int)((_events[i].t - _base) / 3600);
    if (hoff < 0 || hoff >= kHours) continue;
    int x = sx + sw * hoff / kHours;
    g.drawFastVLine(x, sy, sh, evColor(_events[i]));            // line at its true position
    int j = (int)order.size();                                 // insertion sort (small N)
    order.push_back(i);
    while (j > 0 && _events[order[j - 1]].t > _events[i].t) { order[j] = order[j - 1]; j--; }
    order[j] = i;
  }
  int lastRight = sx - 100;
  for (int oi : order) {
    const Event& e = _events[oi];
    int x = sx + sw * (int)((e.t - _base) / 3600) / kHours;
    String tag = e.label; int spc = tag.indexOf(' '); if (spc > 0) tag = tag.substring(0, spc);
    int lx = x + 2; if (lx < lastRight + 1) lx = lastRight + 1; // nudge right to clear neighbour
    if (lx > sx + sw - 6) lx = sx + sw - 6;                     // keep on the strip
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(evColor(e));                                // transparent bg: keep shading
    for (int ci = 0; ci < (int)tag.length(); ++ci) {
      int yy = sy + 2 + ci * 8;
      if (yy > sy + sh - 7) break;
      g.drawString(String(tag[ci]), lx, yy);
    }
    lastRight = lx + 5;
  }

  // --- Legend ---
  int ly = sy + sh + 11;
  g.setTextDatum(textdatum_t::top_left);
  int lx = sx;
  auto swatch = [&](Color c, const char* s) {
    g.drawFastVLine(lx, ly + 1, 7, c); lx += 3;
    g.setTextColor(gTheme.dim, gTheme.bg); g.drawString(s, lx, ly);
    lx += (int)strlen(s) * 6 + 7;
  };
  swatch(gTheme.accent, "pass"); swatch(gTheme.warn, "launch"); swatch(gTheme.ok, "sun/moon");
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString("shade=darkness  grey top=cloud  amber base=moon up", sx, ly + 11);

  // --- Verdict ---
  int y = sy + sh + 36;
  g.setTextColor(gTheme.ok, gTheme.bg);
  g.drawString(_verdict, sx, y); y += 13;

  // --- Tonight's sky: the planets + constellations up during the dark window
  // (more useful than a far-off meteor countdown). An ACTIVE shower is still
  // called out, since that IS relevant tonight.
  {
    ShowerInfo ms = meteorShowerInfo(_base);
    if (ms.active) {
      char b[60];
      if (ms.daysToPeak == 0)     snprintf(b, sizeof(b), "Meteors: %s PEAK tonight (ZHR %d)", ms.name, ms.zhr);
      else if (ms.daysToPeak > 0) snprintf(b, sizeof(b), "Meteors: %s active, peak in %dd (ZHR %d)", ms.name, ms.daysToPeak, ms.zhr);
      else                        snprintf(b, sizeof(b), "Meteors: %s active (ZHR %d)", ms.name, ms.zhr);
      g.setTextColor(gTheme.accent, gTheme.bg);
      g.drawString(b, sx, y); y += 13;
    }

    if (_loc.active().valid && _time.synced()) {
      int hDark = 0; float lo = 999;                    // deepest-dark hour tonight (solar midnight)
      for (int h = 0; h < kHours; ++h) if (_sunAlt[h] < lo) { lo = _sunAlt[h]; hDark = h; }
      if (lo < -6.0f) {                                 // only when it actually gets dark
        double jd = astro::julianDate(_base + (time_t)hDark * 3600);
        double lat = _loc.active().lat, lon = _loc.active().lon;
        // One combined "what's up" list: naked-eye planets, then constellations
        // with >=3 stars up. Flowed across two lines for room; "+N" only if it
        // still overflows both.
        String items[24]; int ni = 0;
        struct { astro::Planet p; const char* n; } P[] = {
          {astro::Planet::Venus, "Venus"}, {astro::Planet::Mars, "Mars"}, {astro::Planet::Jupiter, "Jupiter"},
          {astro::Planet::Saturn, "Saturn"}, {astro::Planet::Mercury, "Mercury"} };
        for (auto& e : P)
          if (astro::planetState(e.p, jd, lat, lon).elDeg > 0 && ni < 24) items[ni++] = e.n;
        double latRad = lat * astro::DEG2RAD, lst = astro::lstRad(jd, lon);
        for (int c = 0; c < kConCount && ni < 24; ++c) {       // constellation up = label centre above horizon
          astro::Equatorial eq{ kCons[c].raHours * 15.0 * astro::DEG2RAD, kCons[c].decDeg * astro::DEG2RAD };
          if (astro::equatorialToHorizontal(eq, latRad, lst).altRad > 0) items[ni++] = kCons[c].name;
        }

        const int maxChars = (cw - sx - 4) / 6;         // chars per line at size 1
        String l0 = "Tonight: ", l1; int i = 0;
        while (i < ni) { String t = (l0.length() > 9 ? l0 + ", " : l0) + items[i];
                         if ((int)t.length() <= maxChars) { l0 = t; i++; } else break; }
        while (i < ni) { String t = (l1.length() ? l1 + ", " : l1) + items[i];
                         if ((int)t.length() <= maxChars) { l1 = t; i++; } else break; }
        if (i < ni) l1 += " +" + String(ni - i);
        if (ni == 0) l0 += "quiet sky";
        g.setTextColor(gTheme.fg, gTheme.bg);
        g.drawString(l0, sx, y); y += 13;
        if (l1.length()) { g.drawString(l1, sx, y); y += 13; }
      }
    }
  }

  // --- Event list (vertical-swipe scrollable) ---
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString("Upcoming", sx, y);
  int headerY = y; y += 13;
  _listY0 = y;
  int total = (int)_events.size();
  if (_listScroll > total - 1) _listScroll = total > 0 ? total - 1 : 0;
  if (_listScroll < 0) _listScroll = 0;
  int avail = (cy0 + ch - 12 - y) / 13; if (avail < 0) avail = 0;
  _listN = total - _listScroll; if (_listN > avail) _listN = avail; if (_listN < 0) _listN = 0;
  if (total > avail && total > 0) {                 // scroll-position indicator on the header
    g.setTextDatum(textdatum_t::top_right);
    g.drawString(String(_listScroll + 1) + "-" + String(_listScroll + _listN) + "/" + String(total), cw - 4, headerY);
  }
  for (int k = 0; k < _listN; ++k) {
    const Event& e = _events[_listScroll + k];
    struct tm tm; time_t t = e.t; localtime_r(&t, &tm);
    char hm[8]; snprintf(hm, sizeof(hm), "%02d:%02d", tm.tm_hour, tm.tm_min);
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(e.kind == 1 ? gTheme.warn : e.kind == 2 ? gTheme.ok : gTheme.accent, gTheme.bg);
    g.drawString(hm, sx, y);
    int cl = _wx.cloudCoverAt(e.t);
    int labelX = sx + 40, cloudX = cw - 4, cloudW = cl >= 0 ? 34 : 0;  // "NN% cld"
    int labelMax = (cloudX - cloudW - labelX) / 6;
    g.setTextColor(gTheme.fg, gTheme.bg);
    g.drawString(e.label.substring(0, labelMax), labelX, y);
    if (cl >= 0) {                                   // cloud cover at the event's time
      g.setTextDatum(textdatum_t::top_right);
      g.setTextColor(cl < 35 ? gTheme.ok : cl < 70 ? gTheme.accent : gTheme.dim, gTheme.bg);
      g.drawString(String(cl) + "% cld", cloudX, y);
    }
    y += 13;
  }
  if (_events.empty()) { g.setTextColor(gTheme.dim, gTheme.bg); g.drawString("no passes or launches in 24h", sx, y); }
}
