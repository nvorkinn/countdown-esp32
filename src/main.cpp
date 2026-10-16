#include <Arduino.h>
#include <esp_heap_caps.h>

#include "device_name.h"
#include "display_registrar.h"
#include "epd_display.h"
#include "esp_epd_bus.h"
#include "esp_ports.h"
#include "frame_client.h"
#include "log_buffer.h"
#include "root_certs.h"
#include "wifi_setup.h"

#if defined(EPD_PANEL_7IN5_V2) == defined(EPD_PANEL_IT8951)
#error "Select exactly one panel: EPD_PANEL_7IN5_V2 or EPD_PANEL_IT8951"
#elif defined(EPD_PANEL_7IN5_V2)
#include "epd7in5_v2.h"
#else
#include "it8951.h"
#if !defined(EPD_PIN_MISO) || EPD_PIN_MISO < 0
#error "IT8951 requires an EPD_PIN_MISO GPIO"
#endif
#ifndef IT8951_VCOM_MV
#define IT8951_VCOM_MV 0
#endif
static_assert(IT8951_VCOM_MV >= 0 && IT8951_VCOM_MV <= 5000, "VCOM must be a magnitude in mV (0 preserves it)");
#endif

#ifndef BROKER_URL
#error "BROKER_URL isn't set: it comes from build_flags in platformio.ini"
#endif
#if !defined(EPD_PIN_DIN) || !defined(EPD_PIN_CLK) || !defined(EPD_PIN_CS) || !defined(EPD_PIN_DC) || \
    !defined(EPD_PIN_RST) || !defined(EPD_PIN_BUSY) || !defined(EPD_PIN_PWR)
#error "The e-paper panel's pins (EPD_PIN_*) come from build_flags in platformio.ini"
#endif

namespace {
constexpr char kNvsNamespace[] = "countdown";

countdown::FrameClient* client = nullptr;
}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println("countdown-esp started");
  if (psramFound()) {
    heap_caps_malloc_extmem_enable(16 * 1024);
  }

  static NvsStore store(kNvsNamespace);
  static SerialLog serialLog;
  static EspClock clock;
  // The hardware RNG is used after Wi-Fi is on, when it has full entropy.
  static EspRandom random;
  // Everything logs through this, so the broker gets the lines from Wi-Fi set-up on.
  static countdown::LogBuffer log(serialLog, clock);
  connectWifi(store, random, log, BROKER_URL);

  static DelaySleeper sleeper;
  static EspMetrics metrics;
#if defined(EPD_PANEL_7IN5_V2)
  static EspEpdBus epdBus({EPD_PIN_DIN, EPD_PIN_CLK, EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY, EPD_PIN_PWR});
  static countdown::Epd7in5V2 panel(epdBus);
#else
  static EspEpdBus epdBus({EPD_PIN_DIN, EPD_PIN_CLK, EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY,
                          EPD_PIN_PWR, EPD_PIN_MISO});
  static countdown::It8951 panel(epdBus, log, IT8951_VCOM_MV);
#endif
  static EspHttp http(kBrokerRootCerts, panel.frameBytes());
  static countdown::DisplayRegistrar registrar(BROKER_URL, store, http, random, sleeper, log,
                                               panel.width(), panel.height());
  static countdown::EpdDisplay display(panel, log);
  static countdown::FrameClient frameClient(registrar, http, display, sleeper, log, metrics, kLogService, kDeviceName);
  client = &frameClient;
}

void loop() { client->tick(); }
