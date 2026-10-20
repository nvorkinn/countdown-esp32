#pragma once

#include <cstdint>
#include <vector>

#include "epd_panel.h"
#include "ports.h"

namespace countdown {

// When a new frame is drawn as a partial refresh of what changed, instead of a full one.
struct PartialPolicy {
  // The changed window may cover at most this share of the screen (percent). A bigger change is worth the flash.
  int maxAreaPercent = 25;
  // Partial refreshes leave a faint ghost of the old picture; a full one clears them, so every this many in a row
  // is followed by one.
  int maxInARow = 30;
};

// Draws the broker's frames on the selected panel: wake the panel, draw, and put it back into deep sleep.
//
// A panel that can refresh part of the screen gets a partial refresh of the window that differs from the frame
// shown last when it's small enough (see PartialPolicy); the first frame, a big change, every maxInARow-th frame and
// the one after any failure are drawn in full.
class EpdDisplay : public Display {
 public:
  EpdDisplay(EpdPanel& panel, Log& log, PartialPolicy policy = {}) : panel_(panel), log_(log), policy_(policy) {}

  bool show(const std::vector<uint8_t>& frame) override;

 private:
  bool showFull(const std::vector<uint8_t>& frame);
  bool showPartial(const std::vector<uint8_t>& frame, const Window& window);
  // Whether `window` is small enough for a partial refresh, and one is due.
  bool partialWorthIt(const Window& window) const;

  EpdPanel& panel_;
  Log& log_;
  PartialPolicy policy_;
  // The frame on the panel, kept only for a panel that can refresh partially; empty when it's unknown.
  std::vector<uint8_t> shown_;
  int partialsInARow_ = 0;
};

}  // namespace countdown
