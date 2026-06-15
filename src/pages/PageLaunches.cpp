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

void PageLaunches::onData(App& app, ProviderId id) {
  if (id == ProviderId::Launch) {
    int n = (int)_lp.launches().size();
    if (_sel >= n) _sel = n ? n - 1 : 0;
  }
  _dirty = _needClear = true;
}

void PageLaunches::onTouch(App& app, int x, int y) {
  int n = (int)_lp.launches().size();
  if (n == 0) return;
  int third = app.contentW() / 3;
  if (x < third)          { _sel = (_sel - 1 + n) % n; _needClear = true; }
  else if (x > 2 * third) { _sel = (_sel + 1) % n;     _needClear = true; }
  _dirty = true;
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
  const auto& list = _lp.launches();
  if (list.empty()) {
    drawMessage(app, _lp.status() == ProviderStatus::Error ? "launch fetch failed"
                   : _lp.status() == ProviderStatus::Loading ? "loading launches..."
                   : "no upcoming launches");
    return;
  }
  if (_sel >= (int)list.size()) _sel = 0;
  const Launch& l = list[_sel];

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
  line(String(_sel + 1) + "/" + list.size() + (_lp.usingFallback() ? "  (RLL)" : ""), gTheme.dim);

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
  for (int i = 0; i < (int)list.size() && y < cy0 + ch - 12; ++i) {
    if (i == _sel) continue;
    const Launch& u = list[i];
    long s = (long)u.net - (long)now; if (s < 0) s = 0;
    char tm[12];
    if (u.net == 0) snprintf(tm, sizeof(tm), "TBD");
    else if (s >= 86400) snprintf(tm, sizeof(tm), "%ldd", s / 86400);
    else snprintf(tm, sizeof(tm), "%02ld:%02ld", s / 3600, (s % 3600) / 60);
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(gTheme.fg, gTheme.bg);
    g.drawString(u.name.substring(0, 26), x0, y);
    g.fillRect(cw - x0 - 44, y, 44, 12, gTheme.bg);   // clear time cell (right-aligned, shrinks)
    g.setTextDatum(textdatum_t::top_right);
    g.setTextColor(gTheme.dim, gTheme.bg);
    g.drawString(tm, cw - x0, y);
    y += 13;
  }
}
