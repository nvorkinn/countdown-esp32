#pragma once

#include <cstdint>
#include <vector>

#include "epd_panel.h"
#include "ports.h"

namespace countdown {

// Draws the broker's frames on the selected panel:
// wake the panel, draw, and put it back into deep sleep.
class EpdDisplay : public Display {
 public:
  EpdDisplay(EpdPanel& panel, Log& log) : panel_(panel), log_(log) {}

  bool show(const std::vector<uint8_t>& frame) override;

 private:
  EpdPanel& panel_;
  Log& log_;
};

}  // namespace countdown
