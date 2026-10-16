#pragma once

#include <SPI.h>

#include "epd_bus.h"
#include "it8951_bus.h"

// DC/PWR/MISO are -1 when unused by the selected HAT.
struct EpdPins {
  int din;
  int clk;
  int cs;
  int dc;
  int rst;
  int busy;
  int pwr;
  int miso = -1;
};

// Both HAT protocols share HSPI, mode 0 at 4 MHz.
class EspEpdBus : public countdown::EpdBus, public countdown::It8951Bus {
 public:
  explicit EspEpdBus(const EpdPins& pins);

  void powerOn() override;
  void powerOff() override;
  void setReset(bool high) override;
  void command(uint8_t command) override;
  void data(const uint8_t* bytes, size_t length) override;
  bool busy() override;
  void delayMs(uint32_t ms) override;
  uint32_t millis() override;
  void beginTransfer() override;
  void endTransfer() override;
  uint16_t transferWord(uint16_t word) override;
  void write(const uint8_t* bytes, size_t length) override;

 private:
  void write(bool isData, const uint8_t* bytes, size_t length);

  EpdPins pins_;
  SPIClass spi_;
  bool spiStarted_ = false;
};
