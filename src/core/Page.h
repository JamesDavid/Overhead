#pragma once
#include <stdint.h>
#include <Arduino.h>
#include "Ids.h"

class App;

// core/Page — the interface every content page implements (spec §4).
//
// Only the active page renders/ticks. Providers keep updating in the background
// and notify via the EventBus; the App forwards those to the active page's
// onData(), which marks itself dirty for the next tick. Pages draw through the
// App (which exposes the display/renderer) and read colours from gTheme — never
// hardcoded.
//
// PreFocus (Director-supplied entry hints, e.g. {selectSatId:25544}) arrives
// with the Director in milestone 7; onEnter is parameterless for now.
class Page {
public:
  virtual ~Page() = default;

  virtual const char* title() const = 0;

  virtual void onEnter(App& app) {}
  virtual void onExit(App& app) {}
  virtual void tick(App& app, uint32_t nowMs) {}   // dirty-rect redraws
  virtual void onTouch(App& app, int x, int y) {}
  virtual void onScroll(App& app, int dy) {}       // vertical swipe (dy<0 up, dy>0 down)
  virtual void onData(App& app, ProviderId id) {}  // EventBus delivery
  virtual String gridStatus() { return String(); }  // one live token for the 3x3 grid tile

  // Attract-mode step (spec §7). While the Director is resting in AUTO with no
  // specific item to highlight, it calls this on a dwell timer so the page tours
  // its selectable objects and then its alternate views. Returns true when the
  // tour has just completed a FULL cycle (wrapped back to the start) — the signal
  // for the Director to advance to the next page in a multi-page ambient rotation.
  // Pages with nothing to cycle return true (they "complete" immediately).
  virtual bool autoAdvance(App& app) { return true; }
};
