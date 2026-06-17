#include "PageLaunches.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../providers/LaunchProvider.h"
#include "../services/TimeService.h"
#include "../assets/Coastline.h"
#include "../assets/LaunchSites.h"
#include <algorithm>
#include <time.h>

static Color statusColor(const String& abbrev) {
  if (abbrev == "Go" || abbrev == "Success" || abbrev == "In Flight") return gTheme.ok;
  if (abbrev == "TBD" || abbrev == "TBC")                            return gTheme.dim;
  return gTheme.warn;   // Hold / Failure / Partial Failure / unknown
}

static bool precise(const String& p) {
  return p.length() == 0 || p == "Second" || p == "Minute" || p == "Hour";
}

// First token of a launch-site string (before the comma) for compact labels/chips.
static String shortSite(const String& loc) {
  int c = loc.indexOf(',');
  return c > 0 ? loc.substring(0, c) : loc;
}

// Cycle "" (all) -> vals[0] -> vals[1] -> ... -> "" through a distinct-value list.
static String cycleVal(const std::vector<String>& vals, const String& cur) {
  if (cur.length() == 0) return vals.empty() ? String("") : vals[0];
  for (size_t i = 0; i < vals.size(); ++i)
    if (vals[i] == cur) return (i + 1 < vals.size()) ? vals[i + 1] : String("");
  return String("");
}

// Short T-minus/T-plus string ("3d", "02:15", "T+05:01") for compact readouts.
static String tMinus(time_t net, time_t now) {
  long s = (long)net - (long)now;
  bool past = s < 0; if (past) s = -s;
  char b[16];
  if (s >= 86400) snprintf(b, sizeof(b), "%s%ldd %02ldh", past ? "T+" : "T-", s / 86400, (s % 86400) / 3600);
  else            snprintf(b, sizeof(b), "%s%02ld:%02ld",  past ? "T+" : "T-", s / 3600, (s % 3600) / 60);
  return b;
}

// Build the visible launch list: fixed 7-day window, NET-TBD always hidden, then
// the active site/company filters. Also collects the distinct sites/companies in
// the window so the chips can cycle through what's actually upcoming.
void PageLaunches::rebuildFilter() {
  const long win = 604800L;                       // 7 days
  time_t now = time(nullptr);
  const auto& list = _lp.launches();
  auto inWindow = [&](const Launch& l) {
    if (l.net == 0) return false;                 // hide NET-TBD
    long dt = (long)l.net - (long)now;
    return dt <= win && dt >= -3600;              // upcoming within 7d (keep just-launched 1h)
  };

  _sites.clear(); _orgs.clear();
  for (const auto& l : list) {
    if (!inWindow(l)) continue;
    if (l.location.length() && std::find(_sites.begin(), _sites.end(), l.location) == _sites.end()) _sites.push_back(l.location);
    if (l.provider.length() && std::find(_orgs.begin(),  _orgs.end(),  l.provider) == _orgs.end())  _orgs.push_back(l.provider);
  }
  // Drop a selected filter value that's no longer present.
  if (_siteVal.length() && std::find(_sites.begin(), _sites.end(), _siteVal) == _sites.end()) _siteVal = "";
  if (_orgVal.length()  && std::find(_orgs.begin(),  _orgs.end(),  _orgVal)  == _orgs.end())  _orgVal = "";

  _filtered.clear();
  for (int i = 0; i < (int)list.size(); ++i) {
    const Launch& l = list[i];
    if (!inWindow(l)) continue;
    if (_siteVal.length() && l.location != _siteVal) continue;
    if (_orgVal.length()  && l.provider != _orgVal)  continue;
    _filtered.push_back(i);
  }
  if (_sel >= (int)_filtered.size()) _sel = _filtered.empty() ? 0 : (int)_filtered.size() - 1;
}

void PageLaunches::onData(App& app, ProviderId id) {
  if (id == ProviderId::Launch) rebuildFilter();
  _dirty = _needClear = true;
}

void PageLaunches::onTouch(App& app, int x, int y) {
  const int cw = app.contentW(), ch = app.contentH();
  if (y >= ch - 18) {                                  // bottom filter chips
    if (x < cw / 2) _siteVal = cycleVal(_sites, _siteVal);
    else            _orgVal  = cycleVal(_orgs,  _orgVal);
    rebuildFilter(); _sel = 0; _needClear = _dirty = true; return;
  }
  int third = cw / 3;
  if (x >= third && x <= 2 * third) { _map = !_map; _needClear = _dirty = true; return; }  // centre toggles view
  int n = (int)_filtered.size();
  if (n == 0) return;
  if (x < third) _sel = (_sel - 1 + n) % n;
  else           _sel = (_sel + 1) % n;
  _needClear = !_map;                                  // map repaints fully anyway
  _dirty = true;
}

bool PageLaunches::autoAdvance(App&) {
  int n = (int)_filtered.size();                       // tour the filtered launches
  if (n <= 0) return true;
  _sel = (_sel + 1) % n; _needClear = !_map; _dirty = true;
  return _sel == 0;                                    // wrapped = full cycle
}

void PageLaunches::tick(App& app, uint32_t nowMs) {
  uint32_t period = _map ? 60000 : 1000;               // card counts down each second; map is static
  if (!_dirty && nowMs - _lastDraw < period) return;
  _dirty = false;
  _lastDraw = nowMs;
  draw(app);
}

void PageLaunches::drawMessage(App& app, const char* msg) {
  auto& g = app.display().gfx();
  g.fillRect(0, app.contentY(), app.contentW(), app.contentH(), gTheme.bg);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.setTextSize(1);
  g.drawString(msg, app.contentW() / 2, app.contentY() + app.contentH() / 2);
}

void PageLaunches::draw(App& app) {
  rebuildFilter();
  const auto& list = _lp.launches();
  if (_filtered.empty()) {
    drawMessage(app, list.empty()
                  ? (_lp.status() == ProviderStatus::Error ? "launch fetch failed"
                     : _lp.status() == ProviderStatus::Loading ? "loading launches..." : "no upcoming launches")
                  : "none match the filters");
    drawChips(app);
    return;
  }
  if (_sel >= (int)_filtered.size()) _sel = 0;
  if (_map) drawMap(app); else drawCard(app);
  drawChips(app);
}

void PageLaunches::drawCard(App& app) {
  const auto& list = _lp.launches();
  const Launch& l = list[_filtered[_sel]];

  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  if (_needClear) { g.fillRect(0, cy0, cw, ch, gTheme.bg); _needClear = false; }
  time_t now = time(nullptr);

  // Status pill (top-right).
  g.setTextDatum(textdatum_t::top_right);
  g.setTextSize(1);
  g.setTextColor(statusColor(l.statusAbbrev), gTheme.bg);
  g.drawString(l.statusName.length() ? l.statusName : l.statusAbbrev, cw - 6, cy0 + 4);
  if (_lp.status() == ProviderStatus::Stale) {
    g.setTextColor(gTheme.warn, gTheme.bg);
    g.drawString("stale", cw - 6, cy0 + 16);
  }

  int x0 = 6, y = cy0 + 4;
  g.setTextDatum(textdatum_t::top_left);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.setTextSize(2);
  g.drawString(l.name.substring(0, 16), x0, y); y += 22;

  g.setTextSize(1);
  auto line = [&](const String& s, Color c) { if (s.length()) { g.setTextColor(c, gTheme.bg); g.drawString(s, x0, y); y += 13; } };
  // Where it's launching from — surfaced prominently (accent), with country.
  String ctry = launchSiteCountry(l.location);
  line(String("@ ") + l.location, gTheme.accent);
  line(l.provider + (l.vehicle.length() ? "  -  " + l.vehicle : String()) + (ctry.length() ? "  (" + ctry + ")" : String()), gTheme.fg);
  line(l.pad + (l.mission.length() ? "  -  " + l.mission : String()), gTheme.dim);
  line(String(_sel + 1) + "/" + _filtered.size() + (_lp.usingFallback() ? "  (RLL)" : ""), gTheme.dim);

  // T-minus right under the index line (left-aligned); the list fills the rest.
  g.setTextDatum(textdatum_t::top_left);
  y += 2;
  if (precise(l.netPrecision)) {
    long s = (long)l.net - (long)now;
    bool past = s < 0; if (past) s = -s;
    long d = s / 86400; s %= 86400; long h = s / 3600; s %= 3600; long m = s / 60, sec = s % 60;
    char b[24];
    if (d > 0) snprintf(b, sizeof(b), "%s%ldd %02ld:%02ld", past ? "T+" : "T-", d, h, m);
    else       snprintf(b, sizeof(b), "%s%02ld:%02ld:%02ld", past ? "T+" : "T-", h, m, sec);
    g.setTextColor(past ? gTheme.warn : gTheme.fg, gTheme.bg);
    g.setTextSize(3); g.drawString(padRight(b, 12), x0, y); y += 28;
  } else {
    struct tm tm; time_t t = l.net; localtime_r(&t, &tm);
    char b[24]; strftime(b, sizeof(b), "~ %b %d", &tm);
    g.setTextColor(gTheme.fg, gTheme.bg); g.setTextSize(2);
    g.drawString(b, x0, y); y += 20;
    g.setTextSize(1); g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(String("precision: ") + l.netPrecision, x0, y); y += 14;
  }

  // Upcoming list fills the remaining space (above the chip row).
  g.drawFastHLine(x0, y, cw - 2 * x0, gTheme.grid); y += 4;
  g.setTextSize(1);
  for (int fi = 0; fi < (int)_filtered.size() && y < cy0 + ch - 30; ++fi) {
    if (fi == _sel) continue;
    const Launch& u = list[_filtered[fi]];
    long s = (long)u.net - (long)now; if (s < 0) s = 0;
    char tm[12];
    if (s >= 86400) snprintf(tm, sizeof(tm), "%ldd", s / 86400);
    else snprintf(tm, sizeof(tm), "%02ld:%02ld", s / 3600, (s % 3600) / 60);
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(gTheme.fg, gTheme.bg);
    int nameMax = (cw - x0 - 48 - x0) / 6;            // fill up to the time cell
    g.drawString(u.name.substring(0, nameMax), x0, y);
    g.fillRect(cw - x0 - 44, y, 44, 12, gTheme.bg);   // clear time cell (right-aligned, shrinks)
    g.setTextDatum(textdatum_t::top_right);
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(tm, cw - x0, y);
    y += 13;
  }
}

// World map (equirectangular) with a marker at each upcoming launch site; the
// selected rocket's site is ringed + labelled. Side-tap cycles the selection.
void PageLaunches::drawMap(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW(), cy0 = app.contentY(), ch = app.contentH();
  g.fillRect(0, cy0, cw, ch, gTheme.bg);            // map repaints fully each time
  _needClear = false;

  const auto& list = _lp.launches();
  const Launch& sel = list[_filtered[_sel]];
  time_t now = time(nullptr);

  // Header: selected launch (name + provider/country + T-minus + index).
  g.setTextDatum(textdatum_t::top_left);
  g.setTextSize(1);
  g.setTextColor(gTheme.accent, gTheme.bg);
  g.drawString(sel.name.substring(0, 32), 4, cy0 + 2);
  String ctry = launchSiteCountry(sel.location);
  g.setTextColor(gTheme.dim, gTheme.bg);
  g.drawString(shortSite(sel.location) + (ctry.length() ? ", " + ctry : String()), 4, cy0 + 14);
  g.setTextDatum(textdatum_t::top_right);
  g.setTextColor(gTheme.fg, gTheme.bg);
  g.drawString(tMinus(sel.net, now) + "  " + String(_sel + 1) + "/" + _filtered.size(), cw - 4, cy0 + 2);

  const int my = cy0 + 26, mh = ch - 26 - 18, mx = 0, mw = cw;
  auto px = [&](float lon) { return mx + (int)round((lon + 180.0f) / 360.0f * mw); };
  auto py = [&](float lat) { return my + (int)round((90.0f - lat) / 180.0f * mh); };

  g.drawRect(mx, my, mw, mh, gTheme.grid);
  g.drawFastHLine(mx, py(0.0f), mw, gTheme.dim);    // equator

  // Coastline (coarse; shared with the Satellites ground track).
  for (int i = 1; i < kCoastlineCount; ++i) {
    const CoastPt& a = kCoastline[i - 1];
    const CoastPt& b = kCoastline[i];
    if (a.lon == 999 || b.lon == 999) continue;     // pen-up
    if (abs(a.lon - b.lon) > 180) continue;         // seam
    g.drawLine(px(a.lon), py(a.lat), px(b.lon), py(b.lat), gTheme.dim);
  }

  // Markers for every filtered launch's site; selected one ringed + labelled last.
  int selX = -1, selY = -1; bool selKnown = false;
  for (int fi = 0; fi < (int)_filtered.size(); ++fi) {
    const Launch& l = list[_filtered[fi]];
    float lat, lon; String c;
    if (!launchSiteLatLon(l.location, lat, lon, c)) continue;   // unknown site -> skip dot
    int sx = px(lon), sy = py(lat);
    if (fi == _sel) { selX = sx; selY = sy; selKnown = true; continue; }   // draw selected on top
    g.fillCircle(sx, sy, 2, gTheme.warn);
  }
  if (selKnown) {
    g.fillCircle(selX, selY, 4, gTheme.ok);
    g.drawCircle(selX, selY, 7, gTheme.ok);
    g.setTextColor(gTheme.ok, gTheme.bg);
    String lbl = shortSite(sel.location);
    int maxRight = (cw - (selX + 9)) / 6, maxLeft = (selX - 9) / 6;   // chars that fit each side
    if (maxRight >= maxLeft) {
      g.setTextDatum(textdatum_t::bottom_left);
      g.drawString(lbl.substring(0, maxRight), selX + 9, selY - 1);
    } else {
      g.setTextDatum(textdatum_t::bottom_right);
      g.drawString(lbl.substring(0, maxLeft), selX - 9, selY - 1);
    }
  } else {
    g.setTextColor(gTheme.warn, gTheme.bg);
    g.setTextDatum(textdatum_t::bottom_left);
    g.drawString("site not mapped", mx + 4, my + mh - 2);
  }
}

void PageLaunches::drawChips(App& app) {
  auto& g = app.display().gfx();
  const int cw = app.contentW();
  int y = app.contentY() + app.contentH() - 16;
  int hw = cw / 2;
  g.setTextSize(1);
  g.setTextDatum(textdatum_t::middle_left);
  auto chip = [&](int x0, int w, const String& label, bool active) {
    g.fillRect(x0, y, w - 2, 14, gTheme.grid);
    g.setTextColor(active ? gTheme.ok : gTheme.fg, gTheme.grid);
    g.drawString(label.substring(0, (w - 10) / 6), x0 + 4, y + 7);
  };
  String s = _siteVal.length() ? shortSite(_siteVal) : String("all");
  String o = _orgVal.length()  ? _orgVal             : String("all");
  chip(2, hw, String("site:") + s, _siteVal.length());
  chip(hw + 2, hw, String("org:") + o, _orgVal.length());
}
