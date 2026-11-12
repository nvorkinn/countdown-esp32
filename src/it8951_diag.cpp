// A utility firmware, built only by the `esp-wrover-kit-it8951-diag` environment (see platformio.ini): it talks to the
// IT8951 HAT with raw GPIO and SPI, and prints what each wire does to the serial port, every 10 seconds. Nothing is
// drawn. It replaces the countdown firmware on the board, so flash the countdown firmware again afterwards.
#include <Arduino.h>
#include <SPI.h>

#if !defined(EPD_PANEL_IT8951) || !defined(EPD_PIN_MISO)
#error "The diagnostics are for the IT8951 HAT: build them with the panel_it8951 flags"
#endif

namespace {
constexpr uint32_t kReadyTimeoutMs = 3000;
constexpr size_t kInfoWords = 20;

SPIClass spi(HSPI);
uint32_t clockHz = 4000000;
int busyDips = 0;  // times HRDY was seen low after a command

bool ready() { return digitalRead(EPD_PIN_BUSY) == HIGH; }

bool waitReady() {
  const uint32_t start = millis();
  while (!ready()) {
    if (millis() - start >= kReadyTimeoutMs) {
      return false;
    }
  }
  return true;
}

// A controller that received a command drops HRDY while it works on it, sometimes for only microseconds.
void watchForBusy() {
  const uint32_t start = micros();
  while (micros() - start < 5000) {
    if (!ready()) {
      ++busyDips;
      return;
    }
  }
}

void endTransfer() {
  digitalWrite(EPD_PIN_CS, HIGH);
  spi.endTransaction();
}

bool beginTransfer(uint16_t preamble) {
  if (!waitReady()) {
    return false;
  }
  spi.beginTransaction(SPISettings(clockHz, MSBFIRST, SPI_MODE0));
  digitalWrite(EPD_PIN_CS, LOW);
  spi.transfer16(preamble);
  if (!waitReady()) {
    endTransfer();
    return false;
  }
  return true;
}

bool command(uint16_t code) {
  if (!beginTransfer(0x6000)) {
    return false;
  }
  spi.transfer16(code);
  endTransfer();
  watchForBusy();
  return true;
}

bool readWords(uint16_t* words, size_t count) {
  if (!beginTransfer(0x1000)) {
    return false;
  }
  spi.transfer16(0);  // dummy word
  if (!waitReady()) {
    endTransfer();
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    words[i] = spi.transfer16(0);
  }
  endTransfer();
  return true;
}

// With the HAT driving HRDY the ESP32's weak pulls can't move it; a line that follows them is connected to nothing.
void checkHrdyIsDriven(const char* when) {
  pinMode(EPD_PIN_BUSY, INPUT_PULLDOWN);
  delay(5);
  const int down = digitalRead(EPD_PIN_BUSY);
  pinMode(EPD_PIN_BUSY, INPUT_PULLUP);
  delay(5);
  const int up = digitalRead(EPD_PIN_BUSY);
  pinMode(EPD_PIN_BUSY, INPUT);
  Serial.printf("HRDY %s: reads %d pulled down, %d pulled up -> %s\n", when, down, up,
                down == up ? (down ? "driven HIGH (ready)" : "driven LOW (busy)") : "FLOATING, not driven by the HAT");
}

void reset() {
  digitalWrite(EPD_PIN_RST, HIGH);
  delay(200);
  digitalWrite(EPD_PIN_RST, LOW);
  delay(10);
  Serial.printf("In reset: HRDY=%d MISO=%d\n", digitalRead(EPD_PIN_BUSY), digitalRead(EPD_PIN_MISO));
  checkHrdyIsDriven("in reset");
  digitalWrite(EPD_PIN_RST, HIGH);

  // A controller that boots holds HRDY low until its firmware is up.
  const uint32_t start = millis();
  bool sawLow = false;
  uint32_t readyAt = 0;
  while (millis() - start < 5000) {
    if (!ready()) {
      sawLow = true;
      readyAt = 0;
    } else if (sawLow && readyAt == 0) {
      readyAt = millis() - start;
    } else if (!sawLow && millis() - start >= 1000) {
      break;
    } else if (readyAt != 0 && millis() - start - readyAt >= 200) {
      break;
    }
  }
  if (!sawLow) {
    Serial.println("After reset: HRDY never went low in 1 s (no sign of the controller booting)");
  } else if (readyAt == 0) {
    Serial.println("After reset: HRDY stayed low for 5 s (controller never became ready)");
  } else {
    Serial.printf("After reset: HRDY went low, then ready after %u ms (controller booted)\n", readyAt);
  }
  checkHrdyIsDriven("after reset");
}

void printText(const uint16_t* words, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    for (const char c : {static_cast<char>(words[i] & 0xFF), static_cast<char>(words[i] >> 8)}) {
      Serial.print(c >= 0x20 && c < 0x7F ? c : '.');
    }
  }
}

void readInfo(uint32_t hz) {
  clockHz = hz;
  busyDips = 0;
  uint16_t info[kInfoWords] = {};
  Serial.printf("--- Device info at %u Hz ---\n", hz);
  if (!command(0x0001)) {
    Serial.println("SYS_RUN: HRDY timed out");
    return;
  }
  if (!command(0x0302)) {
    Serial.println("GET_DEV_INFO: HRDY timed out");
    return;
  }
  if (!readWords(info, kInfoWords)) {
    Serial.println("Reading the reply: HRDY timed out");
    return;
  }
  Serial.printf("HRDY dipped after %d of 2 commands\n", busyDips);
  Serial.print("Raw:");
  for (const uint16_t word : info) {
    Serial.printf(" %04X", word);
  }
  Serial.printf("\nPanel %ux%u, image buffer 0x%04X%04X, firmware \"", info[0], info[1], info[3], info[2]);
  printText(info + 4, 8);
  Serial.print("\", LUT \"");
  printText(info + 12, 8);
  Serial.println("\"");
}
}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(EPD_PIN_CS, OUTPUT);
  digitalWrite(EPD_PIN_CS, HIGH);
  pinMode(EPD_PIN_RST, OUTPUT);
  pinMode(EPD_PIN_BUSY, INPUT);
  spi.begin(EPD_PIN_CLK, EPD_PIN_MISO, EPD_PIN_DIN, -1);
}

void loop() {
  Serial.printf("\n===== IT8951 diagnostics: MOSI=%d MISO=%d SCK=%d CS=%d RST=%d HRDY=%d =====\n", EPD_PIN_DIN,
                EPD_PIN_MISO, EPD_PIN_CLK, EPD_PIN_CS, EPD_PIN_RST, EPD_PIN_BUSY);
  reset();
  // 4 MHz first: straight after the reset, it's what the countdown firmware does.
  for (const uint32_t hz : {4000000u, 1000000u, 250000u}) {
    readInfo(hz);
  }
  delay(10000);
}
