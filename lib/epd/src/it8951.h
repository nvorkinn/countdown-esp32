#pragma once

#include <initializer_list>

#include "epd_panel.h"
#include "it8951_bus.h"
#include "ports.h"

namespace countdown {

class It8951 : public EpdPanel {
 public:
  static constexpr int kWidth = 1872;
  static constexpr int kHeight = 1404;
  static constexpr size_t kFrameBytes = kWidth / 8 * kHeight;
  static constexpr uint32_t kReadyTimeoutMs = 10000;
  static constexpr uint32_t kRefreshTimeoutMs = 60000;

  // Zero preserves the controller's VCOM; otherwise use the panel label's magnitude in mV.
  It8951(It8951Bus& bus, Log& log, uint16_t vcomMv = 0) : bus_(bus), log_(log), vcomMv_(vcomMv) {}

  int width() const override { return kWidth; }
  int height() const override { return kHeight; }
  bool init() override;
  bool display(const uint8_t* frame) override;
  bool sleep() override;

 private:
  bool waitReady();
  bool beginTransfer(uint16_t preamble);
  bool writeWord(uint16_t preamble, uint16_t word);
  bool command(uint16_t command, std::initializer_list<uint16_t> args = {});
  bool readWords(uint16_t* words, size_t count);
  bool readRegister(uint16_t address, uint16_t& value);
  bool writeRegister(uint16_t address, uint16_t value);
  bool waitRefresh();
  bool fail();

  It8951Bus& bus_;
  Log& log_;
  uint16_t vcomMv_;
  uint16_t memoryLow_ = 0;
  uint16_t memoryHigh_ = 0;
  bool initialized_ = false;
};

}  // namespace countdown
