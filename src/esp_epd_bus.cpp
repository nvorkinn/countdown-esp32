#include "esp_epd_bus.h"

#include <Arduino.h>

namespace {
const SPISettings kSpiSettings(4000000, MSBFIRST, SPI_MODE0);
}  // namespace

EspEpdBus::EspEpdBus(const EpdPins& pins) : pins_(pins), spi_(HSPI) {
  pinMode(pins_.cs, OUTPUT);
  digitalWrite(pins_.cs, HIGH);
  if (pins_.dc >= 0) {
    pinMode(pins_.dc, OUTPUT);
  }
  pinMode(pins_.rst, OUTPUT);
  pinMode(pins_.busy, INPUT);
  if (pins_.pwr >= 0) {
    pinMode(pins_.pwr, OUTPUT);
    digitalWrite(pins_.pwr, LOW);  // the HAT stays off until a frame comes
  }
}

void EspEpdBus::powerOn() {
  if (pins_.pwr >= 0) {
    digitalWrite(pins_.pwr, HIGH);
  }
  if (!spiStarted_) {
    spi_.begin(pins_.clk, pins_.miso, pins_.din, -1);
    spiStarted_ = true;
  }
}

void EspEpdBus::powerOff() {
  if (spiStarted_) {
    spi_.end();
    spiStarted_ = false;
  }
  digitalWrite(pins_.rst, LOW);
  if (pins_.dc >= 0) {
    digitalWrite(pins_.dc, LOW);
  }
  if (pins_.pwr >= 0) {
    digitalWrite(pins_.pwr, LOW);
  }
}

void EspEpdBus::setReset(bool high) { digitalWrite(pins_.rst, high ? HIGH : LOW); }

void EspEpdBus::command(uint8_t command) { write(false, &command, 1); }

void EspEpdBus::data(const uint8_t* bytes, size_t length) { write(true, bytes, length); }

bool EspEpdBus::busy() { return digitalRead(pins_.busy) == LOW; }

void EspEpdBus::delayMs(uint32_t ms) { delay(ms); }

uint32_t EspEpdBus::millis() { return ::millis(); }

void EspEpdBus::write(bool isData, const uint8_t* bytes, size_t length) {
  digitalWrite(pins_.dc, isData ? HIGH : LOW);
  beginTransfer();
  // writeBytes, not transferBytes: transferBytes would overwrite the frame with what it reads back.
  write(bytes, length);
  endTransfer();
}

void EspEpdBus::beginTransfer() {
  spi_.beginTransaction(kSpiSettings);
  digitalWrite(pins_.cs, LOW);
}

void EspEpdBus::endTransfer() {
  digitalWrite(pins_.cs, HIGH);
  spi_.endTransaction();
}

uint16_t EspEpdBus::transferWord(uint16_t word) {
  const uint16_t high = spi_.transfer(word >> 8);
  return static_cast<uint16_t>((high << 8) | spi_.transfer(word & 0xFF));
}

void EspEpdBus::write(const uint8_t* bytes, size_t length) { spi_.writeBytes(bytes, length); }
