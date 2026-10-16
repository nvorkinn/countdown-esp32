#pragma once

#include <cstddef>
#include <cstdint>

namespace countdown {

class EpdPanel {
 public:
  virtual ~EpdPanel() = default;

  virtual int width() const = 0;
  virtual int height() const = 0;
  // Broker frames are row-padded, MSB-first monochrome: 0 is black, 1 is white.
  size_t frameBytes() const { return static_cast<size_t>((width() + 7) / 8) * height(); }

  virtual bool init() = 0;
  virtual bool display(const uint8_t* frame) = 0;
  virtual bool sleep() = 0;
};

}  // namespace countdown
