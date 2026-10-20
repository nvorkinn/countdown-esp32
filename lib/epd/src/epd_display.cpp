#include "epd_display.h"

#include <string>

#include "frame_diff.h"

namespace countdown {

bool EpdDisplay::show(const std::vector<uint8_t>& frame) {
  if (frame.size() != panel_.frameBytes()) {
    log_.error("The frame is " + std::to_string(frame.size()) + " bytes; the panel takes " +
               std::to_string(panel_.frameBytes()));
    return false;
  }
  if (panel_.supportsPartial() && shown_.size() == frame.size()) {
    const auto window = changedWindow(shown_.data(), frame.data(), panel_.width(), panel_.height());
    if (!window) {
      return true;  // The same picture is on the panel already.
    }
    if (partialWorthIt(*window)) {
      return showPartial(frame, *window);
    }
  }
  return showFull(frame);
}

bool EpdDisplay::partialWorthIt(const Window& window) const {
  const long area = static_cast<long>(window.width) * window.height;
  const long screen = static_cast<long>(panel_.width()) * panel_.height();
  return partialsInARow_ < policy_.maxInARow && area * 100 <= screen * policy_.maxAreaPercent;
}

bool EpdDisplay::showFull(const std::vector<uint8_t>& frame) {
  // Whatever happens, the panel's picture is unknown until it has been drawn in full.
  shown_.clear();
  if (!panel_.init()) {
    log_.error("The e-paper panel didn't answer: is it connected and powered?");
    return false;
  }
  if (!panel_.display(frame.data())) {
    log_.error("The e-paper panel was still busy after drawing the frame");
    return false;
  }
  if (!panel_.sleep()) {
    log_.error("The e-paper panel didn't go to sleep");
    return false;
  }
  if (panel_.supportsPartial()) {
    shown_ = frame;
    partialsInARow_ = 0;
  }
  return true;
}

bool EpdDisplay::showPartial(const std::vector<uint8_t>& frame, const Window& window) {
  shown_.clear();
  if (!panel_.initPartial()) {
    log_.error("The e-paper panel didn't answer: is it connected and powered?");
    return false;
  }
  if (!panel_.displayPartial(frame.data(), window)) {
    log_.error("The e-paper panel was still busy after a partial refresh");
    return false;
  }
  if (!panel_.sleep()) {
    log_.error("The e-paper panel didn't go to sleep");
    return false;
  }
  shown_ = frame;
  ++partialsInARow_;
  return true;
}

}  // namespace countdown
