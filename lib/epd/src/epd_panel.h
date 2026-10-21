#pragma once

#include <cstddef>
#include <cstdint>

namespace countdown {

// A rectangle of the frame, in pixels. `x` and `width` are multiples of 8, so it covers whole bytes of a row.
struct Window {
  int x;
  int y;
  int width;
  int height;
};

class EpdPanel {
 public:
  virtual ~EpdPanel() = default;

  virtual int width() const = 0;
  virtual int height() const = 0;
  // Broker frames are row-padded, MSB-first monochrome: 1 is black, 0 is white.
  size_t frameBytes() const { return static_cast<size_t>((width() + 7) / 8) * height(); }

  virtual bool init() = 0;
  virtual bool display(const uint8_t* frame) = 0;
  virtual bool sleep() = 0;

  // Redrawing only part of the picture, without the flashing of a full refresh. A panel that can overrides all
  // three; the default can't, and EpdDisplay then always does a full refresh.
  virtual bool supportsPartial() const { return false; }
  // Like init(), but to draw a window. Partial refreshes leave ghosts behind, so EpdDisplay mixes in full ones.
  virtual bool initPartial() { return false; }
  // Draws `window` of `frame` (the whole frame, laid out as for display()) over what's on the panel; the rest of the
  // picture stays as it is. `previous` is the frame that is on the panel now, same layout: a panel that is powered
  // down between updates has forgotten it, and needs it to know which pixels to drive.
  virtual bool displayPartial(const uint8_t* /*frame*/, const uint8_t* /*previous*/, const Window& /*window*/) {
    return false;
  }
};

}  // namespace countdown
