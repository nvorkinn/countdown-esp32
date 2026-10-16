#include <unity.h>

#include <algorithm>
#include <deque>
#include <limits>
#include <optional>
#include <vector>

#include "epd_display.h"
#include "fake_bus.h"
#include "it8951.h"

using namespace countdown;
using namespace countdown::testing;

namespace {

class Controller : public It8951Bus {
 public:
  Controller() { replyInfo(); }

  void replyInfo() {
    reads.insert(reads.end(), {1872, 1404, 0x1234, 0x0056});
    reads.insert(reads.end(), 16, 0);
    reads.push_back(1500);
  }
  void powerOn() override { ++powerOns; }
  void powerOff() override { ++powerOffs; }
  void setReset(bool high) override { resets.push_back(high); }
  void beginTransfer() override {
    TEST_ASSERT_FALSE(selected);
    selected = true;
    transfers.emplace_back();
  }
  void endTransfer() override {
    TEST_ASSERT_TRUE(selected);
    selected = false;
  }
  uint16_t transferWord(uint16_t word) override {
    TEST_ASSERT_TRUE(selected);
    auto& words = transfers.back();
    words.push_back(word);
    if (words.size() == 1 && hangAfterPreamble == word) {
      stuck = true;
    }
    if (words[0] == 0x6000 && words.size() == 2 && hangAfterCommand == word) {
      stuck = true;
    }
    if (words[0] == 0x1000 && words.size() == 2 && hangAfterDummy) {
      stuck = true;
    }
    if (words[0] == 0x1000 && words.size() > 2) {
      if (reads.empty()) {
        return defaultRead;
      }
      uint16_t value = reads.front();
      reads.pop_front();
      return value;
    }
    return 0;
  }
  void write(const uint8_t* bytes, size_t length) override {
    TEST_ASSERT_TRUE(selected);
    TEST_ASSERT_EQUAL_HEX16(0x0000, transfers.back()[0]);
    pixelBytes += length;
    largestWrite = std::max(largestWrite, length);
    if (firstPixels.empty()) {
      firstPixels.assign(bytes, bytes + 16);
    }
    lastPixels.assign(bytes + length - 16, bytes + length);
  }
  bool busy() override { return stuck; }
  void delayMs(uint32_t ms) override { now += ms; }
  uint32_t millis() override { return now; }

  std::vector<std::vector<uint16_t>> commands() const {
    std::vector<std::vector<uint16_t>> result;
    for (const auto& words : transfers) {
      if (words[0] == 0x6000) {
        result.push_back({words[1]});
      } else if (words[0] == 0x0000 && words.size() == 2 && !result.empty()) {
        result.back().push_back(words[1]);
      }
    }
    return result;
  }

  bool hasCommand(std::initializer_list<uint16_t> expected) const {
    const auto all = commands();
    return std::find(all.begin(), all.end(), std::vector<uint16_t>(expected)) != all.end();
  }

  bool selected = false;
  bool stuck = false;
  bool hangAfterDummy = false;
  std::optional<uint16_t> hangAfterPreamble;
  std::optional<uint16_t> hangAfterCommand;
  uint16_t defaultRead = 0;
  uint32_t now = 0;
  int powerOns = 0;
  int powerOffs = 0;
  size_t pixelBytes = 0;
  size_t largestWrite = 0;
  std::deque<uint16_t> reads;
  std::vector<bool> resets;
  std::vector<std::vector<uint16_t>> transfers;
  std::vector<uint8_t> firstPixels;
  std::vector<uint8_t> lastPixels;
};

struct Screen {
  Controller bus;
  RecordingLog log;
  It8951 panel{bus, log};
  EpdDisplay display{panel, log};
};

void test_it8951_initializes_reads_geometry_and_preserves_vcom() {
  Screen s;
  TEST_ASSERT_TRUE(s.panel.init());
  TEST_ASSERT_EQUAL(1872, s.panel.width());
  TEST_ASSERT_EQUAL(1404, s.panel.height());
  TEST_ASSERT_EQUAL(328536, s.panel.frameBytes());
  TEST_ASSERT_EQUAL(1, s.bus.powerOns);
  TEST_ASSERT_TRUE(s.bus.resets == std::vector<bool>({true, false, true}));
  TEST_ASSERT_EQUAL(410, s.bus.now);
  TEST_ASSERT_TRUE(s.bus.hasCommand({0x0001}));
  TEST_ASSERT_TRUE(s.bus.hasCommand({0x0302}));
  TEST_ASSERT_TRUE(s.bus.hasCommand({0x0011, 0x0004, 1}));
  TEST_ASSERT_TRUE(s.bus.hasCommand({0x0039, 0}));
  TEST_ASSERT_FALSE(s.bus.hasCommand({0x0039, 1, 1500}));
  TEST_ASSERT_FALSE(s.bus.selected);
}

void test_it8951_rejects_a_different_panel() {
  Screen s;
  s.bus.reads[0] = 800;
  TEST_ASSERT_FALSE(s.panel.init());
  TEST_ASSERT_EQUAL(1, s.bus.powerOffs);
  TEST_ASSERT_TRUE(s.log.has(LogLevel::Error, "800x1404"));
  TEST_ASSERT_FALSE(s.bus.hasCommand({0x0039, 0}));
}

void test_it8951_sets_and_verifies_an_explicit_vcom() {
  Screen s;
  s.bus.reads.push_back(2310);
  It8951 panel(s.bus, s.log, 2310);
  TEST_ASSERT_TRUE(panel.init());
  TEST_ASSERT_TRUE(s.bus.hasCommand({0x0039, 1, 2310}));
}

void test_it8951_rejects_a_vcom_that_was_not_applied() {
  Screen s;
  s.bus.reads.push_back(1500);
  It8951 panel(s.bus, s.log, 2310);
  TEST_ASSERT_FALSE(panel.init());
  TEST_ASSERT_TRUE(s.log.has(LogLevel::Error, "didn't accept"));
}

void test_it8951_rejects_unusable_vcom() {
  for (uint16_t vcom : {0, 0xFFFF}) {
    Screen s;
    s.bus.reads.back() = vcom;
    TEST_ASSERT_FALSE(s.panel.init());
    TEST_ASSERT_TRUE(s.log.has(LogLevel::Error, "no usable VCOM"));
  }
}

void test_it8951_draws_the_entire_frame_in_bounded_chunks_then_sleeps() {
  Screen s;
  std::vector<uint8_t> frame(It8951::kFrameBytes, 0xFF);
  frame[0] = 0x80;
  frame[1] = 0x01;
  frame[frame.size() - 2] = 0x00;
  frame.back() = 0xAA;

  TEST_ASSERT_TRUE(s.display.show(frame));

  TEST_ASSERT_EQUAL(2628288, s.bus.pixelBytes);
  TEST_ASSERT_LESS_OR_EQUAL(1024, s.bus.largestWrite);
  const uint8_t first[] = {255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255};
  const uint8_t last[] = {0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(first, s.bus.firstPixels.data(), 16);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(last, s.bus.lastPixels.data(), 16);
  TEST_ASSERT_TRUE(s.bus.hasCommand({0x0011, 0x020A, 0x0056}));
  TEST_ASSERT_TRUE(s.bus.hasCommand({0x0011, 0x0208, 0x1234}));
  TEST_ASSERT_TRUE(s.bus.hasCommand({0x0021, 0x0130, 0, 0, 1872, 1404}));
  TEST_ASSERT_TRUE(s.bus.hasCommand({0x0022}));
  TEST_ASSERT_TRUE(s.bus.hasCommand({0x0034, 0, 0, 1872, 1404, 2}));
  TEST_ASSERT_TRUE(s.bus.hasCommand({0x0003}));
  TEST_ASSERT_EQUAL(1, s.bus.powerOffs);
  TEST_ASSERT_FALSE(s.bus.selected);

  s.bus.replyInfo();
  TEST_ASSERT_TRUE(s.display.show(frame));
  TEST_ASSERT_EQUAL(2, s.bus.powerOns);
}

void test_it8951_rejects_a_7in5_frame_without_touching_hardware() {
  Screen s;
  TEST_ASSERT_FALSE(s.display.show(std::vector<uint8_t>(48000)));
  TEST_ASSERT_EQUAL(0, s.bus.powerOns);
  TEST_ASSERT_TRUE(s.log.has(LogLevel::Error, "the panel takes 328536"));
}

void test_it8951_times_out_and_releases_chip_select_at_each_read_handshake() {
  for (int phase = 0; phase < 4; ++phase) {
    Screen s;
    if (phase == 0) s.bus.stuck = true;
    if (phase == 1) s.bus.hangAfterPreamble = 0x6000;
    if (phase == 2) s.bus.hangAfterPreamble = 0x1000;
    if (phase == 3) s.bus.hangAfterDummy = true;
    TEST_ASSERT_FALSE(s.panel.init());
    TEST_ASSERT_EQUAL(1, s.bus.powerOffs);
    TEST_ASSERT_FALSE(s.bus.selected);
    TEST_ASSERT_GREATER_OR_EQUAL(It8951::kReadyTimeoutMs, s.bus.now);
  }
}

void test_it8951_times_out_when_an_argument_cannot_be_sent() {
  Screen s;
  s.bus.hangAfterCommand = 0x0011;
  TEST_ASSERT_FALSE(s.panel.init());
  TEST_ASSERT_EQUAL(1, s.bus.powerOffs);
  TEST_ASSERT_FALSE(s.bus.selected);
}

void test_it8951_waits_for_lut_completion_not_just_hrdy() {
  Screen s;
  TEST_ASSERT_TRUE(s.panel.init());
  s.bus.defaultRead = 1;
  s.bus.now = std::numeric_limits<uint32_t>::max() - 100;
  const uint32_t start = s.bus.now;
  std::vector<uint8_t> frame(It8951::kFrameBytes);
  TEST_ASSERT_FALSE(s.panel.display(frame.data()));
  TEST_ASSERT_EQUAL(It8951::kRefreshTimeoutMs, s.bus.now - start);
  TEST_ASSERT_EQUAL(0, s.bus.pixelBytes);
  TEST_ASSERT_EQUAL(1, s.bus.powerOffs);
}

void test_it8951_reports_a_refresh_that_never_completes() {
  Screen s;
  TEST_ASSERT_TRUE(s.panel.init());
  s.bus.reads.push_back(0);
  s.bus.defaultRead = 1;
  std::vector<uint8_t> frame(It8951::kFrameBytes);
  TEST_ASSERT_FALSE(s.panel.display(frame.data()));
  TEST_ASSERT_EQUAL(2628288, s.bus.pixelBytes);
  TEST_ASSERT_EQUAL(1, s.bus.powerOffs);
}

void test_it8951_handles_failed_image_transfers_and_refresh_commands() {
  for (uint16_t code : {0x0010, 0x0011, 0x0021, 0x0022, 0x0034}) {
    Screen s;
    TEST_ASSERT_TRUE(s.panel.init());
    s.bus.hangAfterCommand = code;
    std::vector<uint8_t> frame(It8951::kFrameBytes);
    TEST_ASSERT_FALSE(s.panel.display(frame.data()));
    TEST_ASSERT_EQUAL(1, s.bus.powerOffs);
    TEST_ASSERT_FALSE(s.bus.selected);
  }
}

void test_it8951_reports_sleep_failures() {
  for (bool lutBusy : {false, true}) {
    Screen s;
    TEST_ASSERT_TRUE(s.panel.init());
    if (lutBusy) s.bus.defaultRead = 1;
    else s.bus.hangAfterCommand = 0x0003;
    TEST_ASSERT_FALSE(s.panel.sleep());
    TEST_ASSERT_EQUAL(1, s.bus.powerOffs);
  }
}

void test_it8951_rejects_operations_without_initialization() {
  Screen s;
  TEST_ASSERT_FALSE(s.panel.display(nullptr));
  TEST_ASSERT_FALSE(s.panel.sleep());
  TEST_ASSERT_TRUE(s.log.has(LogLevel::Error, "initialized before drawing"));
  TEST_ASSERT_TRUE(s.log.has(LogLevel::Error, "initialized before sleeping"));
}

}  // namespace

void runIt8951Tests() {
  RUN_TEST(test_it8951_initializes_reads_geometry_and_preserves_vcom);
  RUN_TEST(test_it8951_rejects_a_different_panel);
  RUN_TEST(test_it8951_sets_and_verifies_an_explicit_vcom);
  RUN_TEST(test_it8951_rejects_a_vcom_that_was_not_applied);
  RUN_TEST(test_it8951_rejects_unusable_vcom);
  RUN_TEST(test_it8951_draws_the_entire_frame_in_bounded_chunks_then_sleeps);
  RUN_TEST(test_it8951_rejects_a_7in5_frame_without_touching_hardware);
  RUN_TEST(test_it8951_times_out_and_releases_chip_select_at_each_read_handshake);
  RUN_TEST(test_it8951_times_out_when_an_argument_cannot_be_sent);
  RUN_TEST(test_it8951_waits_for_lut_completion_not_just_hrdy);
  RUN_TEST(test_it8951_reports_a_refresh_that_never_completes);
  RUN_TEST(test_it8951_handles_failed_image_transfers_and_refresh_commands);
  RUN_TEST(test_it8951_reports_sleep_failures);
  RUN_TEST(test_it8951_rejects_operations_without_initialization);
}
