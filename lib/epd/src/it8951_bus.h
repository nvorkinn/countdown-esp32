#pragma once

#include <cstddef>
#include <cstdint>

namespace countdown {

// IT8951 uses 16-bit SPI preambles and MISO, not the small panels' DC pin protocol.
class It8951Bus {
 public:
  virtual ~It8951Bus() = default;
  virtual void powerOn() = 0;
  virtual void powerOff() = 0;
  virtual void setReset(bool high) = 0;
  virtual void beginTransfer() = 0;
  virtual void endTransfer() = 0;
  virtual uint16_t transferWord(uint16_t word) = 0;
  virtual void write(const uint8_t* bytes, size_t length) = 0;
  virtual bool busy() = 0;
  virtual void delayMs(uint32_t ms) = 0;
  virtual uint32_t millis() = 0;
};

}  // namespace countdown
