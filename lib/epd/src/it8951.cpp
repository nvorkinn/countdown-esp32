#include "it8951.h"

#include <string>

namespace countdown {
namespace {
constexpr uint16_t kWriteCommand = 0x6000;
constexpr uint16_t kWriteData = 0x0000;
constexpr uint16_t kReadData = 0x1000;
constexpr uint16_t kImageAddress = 0x0208;
constexpr uint16_t kLutStatus = 0x1224;
}  // namespace

bool It8951::init() {
  initialized_ = false;
  bus_.powerOn();
  bus_.setReset(true);
  bus_.delayMs(200);
  bus_.setReset(false);
  bus_.delayMs(10);
  bus_.setReset(true);
  bus_.delayMs(200);

  uint16_t info[20];
  if (!command(0x0001) || !command(0x0302) || !readWords(info, 20)) {
    return fail();
  }
  if (info[0] != kWidth || info[1] != kHeight) {
    log_.error("IT8951 reports " + std::to_string(info[0]) + "x" + std::to_string(info[1]) +
               "; this build requires 1872x1404");
    return fail();
  }
  memoryLow_ = info[2];
  memoryHigh_ = info[3];
  if (!writeRegister(0x0004, 1)) {  // Enable packed image writes.
    return fail();
  }
  uint16_t vcom;
  if (!command(0x0039, {0}) || !readWords(&vcom, 1)) {
    return fail();
  }
  if (vcomMv_ != 0 && vcom != vcomMv_) {
    if (!command(0x0039, {1, vcomMv_}) || !command(0x0039, {0}) || !readWords(&vcom, 1)) {
      return fail();
    }
    if (vcom != vcomMv_) {
      log_.error("IT8951 didn't accept the configured VCOM");
      return fail();
    }
  }
  if (vcom == 0 || vcom > 5000) {
    log_.error("IT8951 has no usable VCOM; configure IT8951_VCOM_MV from the panel label");
    return fail();
  }
  log_.info("IT8951 VCOM is -" + std::to_string(vcom) + " mV; it must match the panel label");
  initialized_ = true;
  return true;
}

bool It8951::display(const uint8_t* frame) {
  if (!initialized_) {
    log_.error("IT8951 must be initialized before drawing");
    return fail();
  }
  if (!waitRefresh() || !writeRegister(kImageAddress + 2, memoryHigh_) ||
      !writeRegister(kImageAddress, memoryLow_) ||
      !command(0x0021, {0x0130, 0, 0, kWidth, kHeight}) || !beginTransfer(kWriteData)) {
    return fail();
  }

  // Big-endian 8-bpp pixels avoid firmware-specific 1-bpp modes. Expand only a small
  // chunk at a time; the broker's monochrome buffer remains the only frame-sized allocation.
  uint8_t pixels[1024];
  for (size_t offset = 0; offset < kFrameBytes;) {
    const size_t count = kFrameBytes - offset < sizeof(pixels) / 8 ? kFrameBytes - offset : sizeof(pixels) / 8;
    for (size_t i = 0; i < count; ++i) {
      for (size_t bit = 0; bit < 8; ++bit) {
        pixels[i * 8 + bit] = (frame[offset + i] & (0x80 >> bit)) ? 0xFF : 0x00;
      }
    }
    bus_.write(pixels, count * 8);
    offset += count;
    bus_.delayMs(1);  // Yield while transferring the large image.
  }
  bus_.endTransfer();
  // GC16 is a full refresh; unlike A2 it doesn't depend on the panel's waveform version.
  if (!command(0x0022) || !command(0x0034, {0, 0, kWidth, kHeight, 2}) || !waitRefresh()) {
    return fail();
  }
  return true;
}

bool It8951::sleep() {
  if (!initialized_) {
    log_.error("IT8951 must be initialized before sleeping");
    return fail();
  }
  if (!waitRefresh() || !command(0x0003) || !waitReady()) {
    return fail();
  }
  bus_.powerOff();
  initialized_ = false;
  return true;
}

bool It8951::waitReady() {
  const uint32_t start = bus_.millis();
  while (bus_.busy()) {
    if (bus_.millis() - start >= kReadyTimeoutMs) {
      return false;
    }
    bus_.delayMs(1);
  }
  return true;
}

bool It8951::beginTransfer(uint16_t preamble) {
  if (!waitReady()) {
    return false;
  }
  bus_.beginTransfer();
  bus_.transferWord(preamble);
  if (!waitReady()) {
    bus_.endTransfer();
    return false;
  }
  return true;
}

bool It8951::writeWord(uint16_t preamble, uint16_t word) {
  if (!beginTransfer(preamble)) {
    return false;
  }
  bus_.transferWord(word);
  bus_.endTransfer();
  return true;
}

bool It8951::command(uint16_t code, std::initializer_list<uint16_t> args) {
  if (!writeWord(kWriteCommand, code)) {
    return false;
  }
  for (uint16_t arg : args) {
    if (!writeWord(kWriteData, arg)) {
      return false;
    }
  }
  return true;
}

bool It8951::readWords(uint16_t* words, size_t count) {
  if (!beginTransfer(kReadData)) {
    return false;
  }
  bus_.transferWord(0);  // Read transactions start with a dummy word.
  if (!waitReady()) {
    bus_.endTransfer();
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    words[i] = bus_.transferWord(0);
  }
  bus_.endTransfer();
  return true;
}

bool It8951::readRegister(uint16_t address, uint16_t& value) {
  return command(0x0010, {address}) && readWords(&value, 1);
}

bool It8951::writeRegister(uint16_t address, uint16_t value) {
  return command(0x0011, {address, value});
}

bool It8951::waitRefresh() {
  const uint32_t start = bus_.millis();
  while (true) {
    uint16_t status;
    if (!readRegister(kLutStatus, status)) {
      return false;
    }
    if (status == 0) {
      return true;
    }
    if (bus_.millis() - start >= kRefreshTimeoutMs) {
      return false;
    }
    bus_.delayMs(20);
  }
}

bool It8951::fail() {
  bus_.powerOff();
  initialized_ = false;
  return false;
}

}  // namespace countdown
