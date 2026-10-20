#include "esp_ports.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <algorithm>
#include <vector>

#include "response_body.h"
#include "retry.h"

NvsStore::NvsStore(const char* name) { preferences_.begin(name); }

std::optional<std::string> NvsStore::get(const std::string& key) {
  if (!preferences_.isKey(key.c_str())) {
    return std::nullopt;
  }
  return std::string(preferences_.getString(key.c_str()).c_str());
}

void NvsStore::put(const std::string& key, const std::string& value) {
  preferences_.putString(key.c_str(), value.c_str());
}

namespace {

// Whether a buffer this big can be allocated: the largest free block, in PSRAM (where big buffers
// go, see main.cpp) or in internal RAM. Built without exceptions, a failed allocation aborts, so
// ResponseBody asks first and reports it instead.
bool fitsInMemory(size_t bytes) {
  return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) >= bytes ||
         heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= bytes;
}

// Stream HTTPClient's fixed-length or chunked body into the bounded receiver.
class VectorStream : public Stream {
 public:
  explicit VectorStream(countdown::ResponseBody& out) : out_(out) {}
  size_t write(uint8_t byte) override { return out_.append(&byte, 1); }
  size_t write(const uint8_t* buffer, size_t size) override {
    return out_.append(buffer, size);
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  countdown::ResponseBody& out_;
};

}  // namespace

EspHttp::EspHttp(const char* rootCerts, size_t maxBodyBytes) : maxBodyBytes_(maxBodyBytes) {
  client_.setCACert(rootCerts);
}

countdown::HttpResponse EspHttp::request(const std::string& method, const std::string& url,
                                         const std::string& jsonBody, const std::string& bearer,
                                         const std::string& ifNoneMatch) {
  countdown::HttpResponse response;
  if (maxBodyBytes_ > 128 * 1024 && !psramFound()) {
    response.error = "This panel requires working PSRAM for its framebuffer";
    return response;
  }
  HTTPClient http;
  http.setConnectTimeout(countdown::kRequestTimeoutS * 1000);
  http.setTimeout(countdown::kRequestTimeoutS * 1000);
  if (!http.begin(client_, url.c_str())) {
    response.error = "couldn't parse " + url;
    return response;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", ("Bearer " + bearer).c_str());
  if (!ifNoneMatch.empty()) {
    http.addHeader("If-None-Match", ifNoneMatch.c_str());
  }
  const char* collected[] = {"Retry-After", "ETag"};
  http.collectHeaders(collected, 2);

  const int status = http.sendRequest(method.c_str(), reinterpret_cast<uint8_t*>(const_cast<char*>(jsonBody.data())),
                                      jsonBody.size());
  if (status < 0) {
    response.error = HTTPClient::errorToString(status).c_str();
    http.end();
    return response;
  }
  response.status = status;
  if (http.hasHeader("Retry-After")) {
    response.retryAfter = http.header("Retry-After").c_str();
  }
  if (http.hasHeader("ETag")) {
    response.etag = http.header("ETag").c_str();
  }
  const int size = http.getSize();
  if (countdown::responseHasBody(status) && size != 0) {
    countdown::ResponseBody buffer(response.body, maxBodyBytes_, fitsInMemory);
    if (size > 0 && !buffer.reserve(static_cast<size_t>(size))) {
      response.error = buffer.error();
      http.end();
      return response;
    }
    VectorStream body(buffer);
    const int received = http.writeToStream(&body);
    if (buffer.error() || received < 0 || (size > 0 && received != size)) {
      response.error = buffer.error() ? buffer.error() : "Couldn't read the complete HTTP response";
      response.body.clear();
      http.end();
      return response;
    }
  }
  response.sent = true;
  http.end();
  return response;
}

void EspRandom::fill(uint8_t* buffer, size_t length) { esp_fill_random(buffer, length); }

void DelaySleeper::sleepMs(uint64_t milliseconds) {
  // delay() takes 32 bits of milliseconds, about 49 days.
  while (milliseconds > 0) {
    const uint32_t step = static_cast<uint32_t>(std::min<uint64_t>(milliseconds, UINT32_MAX));
    delay(step);
    milliseconds -= step;
  }
}

uint64_t EspClock::uptimeMs() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }

std::vector<countdown::Metric> EspMetrics::read() {
  return {
      {"uptime_s", esp_timer_get_time() / 1000000},
      {"free_heap", ESP.getFreeHeap()},
      {"min_free_heap", ESP.getMinFreeHeap()},
      {"free_psram", ESP.getFreePsram()},
      {"wifi_rssi", WiFi.RSSI()},
      // esp_reset_reason_t: 1 power-on, 3 software restart, 4 panic, 5-7 watchdogs, 8 deep sleep, 9 brownout.
      {"reset_reason", esp_reset_reason()},
  };
}

void SerialLog::write(countdown::LogLevel level, const std::string& message) {
  const char* name = level == countdown::LogLevel::Error     ? "ERROR"
                     : level == countdown::LogLevel::Warning ? "WARNING"
                                                             : "INFO";
  Serial.printf("%s %s\n", name, message.c_str());
}
