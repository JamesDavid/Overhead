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
#include "../astro/SolarSystem.h"
#include "../assets/MeteorShowers.h"
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
          _events.push_back({ p.aos, lbl, 0 });
        }
        break;
      }
    }
  }
  for (const auto& l : _launch.launches())
    if (l.net && l.net > now && l.net < horizon)
      _events.push_back({ l.net, l.name, 1 });

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

void PageAgenda::tick(App& app, uint32_t nowMs) {
  // Heavy recompute (pass prediction for the watchlist) only every 5 min or on a
  // location change — NOT on every provider publish (that starved the loop/touch).
  bool ready = _time.synced() && _loc.active().valid;
  if (ready && (!_computed || nowMs - _lastRecompute > 300000)) {
    recompute(); _lastRecompute = nowMs; _computed = true; _dirty = true;
  }
  if (!_dirty && nowMs - _lastDraw < 15000) return;   // cheap redraw, <=15s
  _dirty = false; _lastDraw = nowMs;
  draw(app);
}

void PageAgenda::onTouch(App& app, int x, int y) {
  // Tap an Upcoming row -> jump to that event's tab; centre / empty area -> 3x3 grid.
  if (!_events.empty() && _listN > 0) {
    int row = (y + app.contentY() - _listY0) / 13;          // which Upcoming row
    if (row >= 0 && row < _listN && row < (int)_events.size()) {
      const char* tab = _events[row].kind == 1 ? "Launches"
                      : _events[row].kind == 2 ? "Solar System" : "Satellites";
      int idx = app.pageIndexByTitle(tab);
      if (idx >= 0) app.setPage(idx);
      return;
    }
  }
  app.openGrid();
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

  // --- Meteor showers: active shower or a countdown to the next peak ---
  {
    ShowerInfo ms = meteorShowerInfo(_base);
    char b[60];
    if (ms.active) {
      if (ms.daysToPeak > 0)       snprintf(b, sizeof(b), "Meteors: %s active, peak in %dd (ZHR %d)", ms.name, ms.daysToPeak, ms.zhr);
      else if (ms.daysToPeak == 0) snprintf(b, sizeof(b), "Meteors: %s PEAK tonight (ZHR %d)", ms.name, ms.zhr);
      else                         snprintf(b, sizeof(b), "Meteors: %s active, peak %dd ago (ZHR %d)", ms.name, -ms.daysToPeak, ms.zhr);
    } else {
      snprintf(b, sizeof(b), "Meteors: next %s in %dd (ZHR %d)", ms.name, ms.daysToPeak, ms.zhr);
    }
    g.setTextColor(ms.active ? gTheme.accent : gTheme.dim, gTheme.bg);
    g.drawString(b, sx, y); y += 13;
  }

  // --- Event list ---
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString("Upcoming", sx, y); y += 13;
  _listY0 = y; _listN = 0;                          // remember list geometry for tap-to-jump
  for (const auto& e : _events) {
    if (y > cy0 + ch - 12) break;
    _listN++;
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
