#include "PageLaunches.h"
#include "../core/App.h"
#include "../core/Theme.h"
#include "../hal/Display.h"
#include "../providers/LaunchProvider.h"
#include "../services/TimeService.h"
#include <time.h>

static Color statusColor(const String& abbrev) {
  if (abbrev == "Go" || abbrev == "Success" || abbrev == "In Flight") return gTheme.ok;
  if (abbrev == "TBD" || abbrev == "TBC")                            return gTheme.dim;
  return gTheme.warn;   // Hold / Failure / Partial Failure / unknown
}

static bool precise(const String& p) {
  return p.length() == 0 || p == "Second" || p == "Minute" || p == "Hour";
}

// Build the visible launch index list for the current time-window + TBD filter.
void PageLaunches::rebuildFilter() {
  _filtered.clear();
  const long win[] = {86400L, 604800L, 2592000L, 0L};   // 24h, 7d, 30d, all
  long w = win[_window];
  time_t now = time(nullptr);
  const auto& list = _lp.launches();
  for (int i = 0; i < (int)list.size(); ++i) {
    const Launch& l = list[i];
    if (l.net == 0) { if (!_hideTBD) _filtered.push_back(i); continue; }   // NET TBD
    if (w == 0 || (long)(l.net - now) <= w) _filtered.push_back(i);
  }
  if (_sel >= (int)_filtered.size()) _sel = _filtered.empty() ? 0 : (int)_filtered.size() - 1;
}

void PageLaunches::onData(App& app, ProviderId id) {
  if (id == ProviderId::Launch) rebuildFilter();
  _dirty = _needClear = true;
}

void PageLaunches::onTouch(App& app, int x, int y) {
  if (y >= app.contentH() - 18) {                    // bottom filter chips
    if (x < 44)      { _window = (_window + 1) % 4; rebuildFilter(); _sel = 0; _needClear = _dirty = true; return; }
    else if (x < 92) { _hideTBD = !_hideTBD;        rebuildFilter(); _sel = 0; _needClear = _dirty = true; return; }
  }
  int n = (int)_filtered.size();
  if (n == 0) return;
  int third = app.contentW() / 3;
  if (x < third)          { _sel = (_sel - 1 + n) % n; _needClear = true; }
  else if (x > 2 * third) { _sel = (_sel + 1) % n;     _needClear = true; }
  _dirty = true;
}

bool PageLaunches::autoAdvance(App&) {
  int n = (int)_filtered.size();           // single view: tour the filtered launches
  if (n <= 0) return true;
  _sel = (_sel + 1) % n; _needClear = _dirty = true;
  return _sel == 0;                        // wrapped = full cycle
}

void PageLaunches::tick(App& app, uint32_t nowMs) {
  if (!_dirty && nowMs - _lastDraw < 1000) return;
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
                  : "none in this window");
    drawBadges(app);
    return;
  }
  if (_sel >= (int)_filtered.size()) _sel = 0;
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
  // Where it's launching from — surfaced prominently (accent).
  line(String("@ ") + l.location, gTheme.accent);
  line(l.provider + (l.vehicle.length() ? "  -  " + l.vehicle : String()), gTheme.fg);
  line(l.pad + (l.mission.length() ? "  -  " + l.mission : String()), gTheme.dim);
  line(String(_sel + 1) + "/" + _filtered.size() + (_lp.usingFallback() ? "  (RLL)" : ""), gTheme.dim);

  // T-minus right under the index line (left-aligned); the list fills the rest.
  g.setTextDatum(textdatum_t::top_left);
  y += 2;
  if (l.net == 0) {
    g.setTextColor(gTheme.dim, gTheme.bg); g.setTextSize(2);
    g.drawString("NET TBD", x0, y); y += 22;
  } else if (precise(l.netPrecision)) {
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

  // Upcoming list fills the remaining space.
  g.drawFastHLine(x0, y, cw - 2 * x0, gTheme.grid); y += 4;
  g.setTextSize(1);
  for (int fi = 0; fi < (int)_filtered.size() && y < cy0 + ch - 16; ++fi) {
    if (fi == _sel) continue;
    const Launch& u = list[_filtered[fi]];
    long s = (long)u.net - (long)now; if (s < 0) s = 0;
    char tm[12];
    if (u.net == 0) snprintf(tm, sizeof(tm), "TBD");
    else if (s >= 86400) snprintf(tm, sizeof(tm), "%ldd", s / 86400);
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
  drawBadges(app);
}

void PageLaunches::drawBadges(App& app) {
  auto& g = app.display().gfx();
  int y = app.contentY() + app.contentH() - 16;
  static const char* win[] = {"24h", "7d", "30d", "all"};
  g.setTextSize(1);
  g.setTextDatum(textdatum_t::middle_left);
  g.fillRect(2, y, 40, 14, gTheme.grid);
  g.setTextColor(gTheme.fg, gTheme.grid);
  g.drawString(win[_window], 6, y + 7);
  g.fillRect(44, y, 46, 14, gTheme.grid);
  g.setTextColor(_hideTBD ? gTheme.dim : gTheme.fg, gTheme.grid);
  g.drawString(_hideTBD ? "-TBD" : "+TBD", 48, y + 7);
}
