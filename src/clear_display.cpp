// A utility firmware, built only by the `-clear` environments (see platformio.ini): it draws an all-white frame on
// the e-paper panel, puts the panel to sleep and does nothing else. It replaces the countdown firmware on the board,
// so flash the countdown firmware again afterwards.
#include <Arduino.h>
#include <esp_heap_caps.h>

#include <vector>

#include "epd_display.h"
#include "esp_epd_bus.h"
#include "esp_ports.h"

#if defined(EPD_PANEL_7IN5_V2) == defined(EPD_PANEL_IT8951)
#error "Select exactly one panel: EPD_PANEL_7IN5_V2 or EPD_PANEL_IT8951"
#elif defined(EPD_PANEL_7IN5_V2)
#include "epd7in5_v2.h"
#else
#include "it8951.h"
#endif

void setup() {
  Serial.begin(115200);
  if (psramFound()) {
    heap_caps_malloc_extmem_enable(16 * 1024);
  }
  static SerialLog log;
#if defined(EPD_PANEL_7IN5_V2)
  static EspEpdBus epdBus({EPD_PIN_DIN, EPD_PIN_CLK, EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY, EPD_PIN_PWR});
  static countdown::Epd7in5V2 panel(epdBus);
#else
  static EspEpdBus epdBus({EPD_PIN_DIN, EPD_PIN_CLK, EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY,
                          EPD_PIN_PWR, EPD_PIN_MISO});
  // VCOM 0 leaves the panel's stored value alone.
  static countdown::It8951 panel(epdBus, log, 0);
#endif
  static countdown::EpdDisplay display(panel, log);
  // Frames are 1 = black, 0 = white: all zeros is a white screen.
  const std::vector<uint8_t> white(panel.frameBytes(), 0);
  Serial.println(display.show(white) ? "Display cleared" : "Clearing the display failed");
}

void loop() { delay(1000); }
