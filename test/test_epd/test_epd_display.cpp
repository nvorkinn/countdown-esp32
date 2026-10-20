#include <unity.h>

#include <vector>

#include "epd7in5_v2.h"
#include "epd_display.h"
#include "fake_bus.h"

using namespace countdown;
using namespace countdown::testing;

namespace {

struct Screen {
  FakeBus bus;
  Epd7in5V2 panel{bus};
  RecordingLog log;
  EpdDisplay display{panel, log};
};

const std::vector<uint8_t> kFrame(Epd7in5V2::kFrameBytes, 0xAA);

void test_a_frame_wakes_the_panel_draws_it_and_puts_it_to_sleep() {
  Screen s;

  TEST_ASSERT_TRUE(s.display.show(kFrame));

  std::vector<uint8_t> order;
  for (const CommandWithData& command : s.bus.commandsWithoutStatusChecks()) {
    if (command.first == 0x04 || command.first == 0x10 || command.first == 0x13 || command.first == 0x12 ||
        command.first == 0x07) {
      order.push_back(command.first);
    }
  }
  const std::vector<uint8_t> expected = {0x04, 0x10, 0x13, 0x12, 0x07};  // power on, draw, deep sleep
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), order.data(), expected.size());
  TEST_ASSERT_EQUAL(expected.size(), order.size());
  TEST_ASSERT_TRUE(s.bus.events.front().kind == Event::Kind::PowerOn);
  TEST_ASSERT_TRUE(s.bus.events.back().kind == Event::Kind::PowerOff);
  TEST_ASSERT_TRUE(s.log.lines.empty());
}

void test_a_frame_of_the_wrong_size_is_refused_without_touching_the_panel() {
  Screen s;

  TEST_ASSERT_FALSE(s.display.show(std::vector<uint8_t>(100, 0xFF)));

  TEST_ASSERT_TRUE(s.bus.events.empty());
  TEST_ASSERT_TRUE(s.log.has(LogLevel::Error, "The frame is 100 bytes; the panel takes 48000"));
}

void test_a_missing_panel_is_reported_and_nothing_is_drawn() {
  Screen s;
  s.bus.stuck = true;

  TEST_ASSERT_FALSE(s.display.show(kFrame));

  TEST_ASSERT_EQUAL(0, s.bus.count(0x10));
  TEST_ASSERT_TRUE(s.log.has(LogLevel::Error, "is it connected and powered?"));
}

void test_a_refresh_that_never_finishes_is_reported() {
  Screen s;
  s.bus.hangAfter = 0x12;

  TEST_ASSERT_FALSE(s.display.show(kFrame));

  TEST_ASSERT_EQUAL(0, s.bus.count(0x07));
  TEST_ASSERT_TRUE(s.log.has(LogLevel::Error, "still busy after drawing"));
}

void test_a_panel_that_wont_sleep_is_reported() {
  Screen s;
  s.bus.hangAfter = 0x02;

  TEST_ASSERT_FALSE(s.display.show(kFrame));

  TEST_ASSERT_TRUE(s.log.has(LogLevel::Error, "didn't go to sleep"));
}

// A copy of kFrame with the rows y..y+rows-1 changed in the byte columns x..x+cols-1.
std::vector<uint8_t> changed(int y, int rows, int x, int cols) {
  std::vector<uint8_t> frame = kFrame;
  for (int row = y; row < y + rows; ++row) {
    for (int col = x; col < x + cols; ++col) {
      frame[row * (Epd7in5V2::kWidth / 8) + col] = 0x55;
    }
  }
  return frame;
}

void test_the_first_frame_is_drawn_in_full() {
  Screen s;

  TEST_ASSERT_TRUE(s.display.show(kFrame));

  TEST_ASSERT_EQUAL(1, s.bus.count(0x10));
  TEST_ASSERT_EQUAL(0, s.bus.count(0x91));
}

void test_a_small_change_is_drawn_as_a_partial_refresh_of_its_window() {
  Screen s;
  s.display.show(kFrame);
  s.bus.events.clear();

  // A progress bar: 8 rows of 20 byte columns starting at column 10.
  TEST_ASSERT_TRUE(s.display.show(changed(400, 8, 10, 20)));

  TEST_ASSERT_EQUAL(0, s.bus.count(0x10));
  TEST_ASSERT_EQUAL(1, s.bus.count(0x91));
  TEST_ASSERT_EQUAL(1, s.bus.count(0x12));
  TEST_ASSERT_EQUAL(1, s.bus.count(0x07));
  bool found = false;
  for (const CommandWithData& command : s.bus.commands()) {
    if (command.first == 0x90) {
      const std::vector<uint8_t> window = {0x00, 80, 0x00, 239, 0x01, 144, 0x01, 151, 0x01};
      TEST_ASSERT_TRUE(command.second == window);
      found = true;
    }
  }
  TEST_ASSERT_TRUE(found);
}

void test_the_same_frame_again_touches_nothing() {
  Screen s;
  s.display.show(kFrame);
  s.bus.events.clear();

  TEST_ASSERT_TRUE(s.display.show(kFrame));

  TEST_ASSERT_TRUE(s.bus.events.empty());
}

void test_a_big_change_is_drawn_in_full() {
  Screen s;
  s.display.show(kFrame);
  s.bus.events.clear();

  // Half the screen's height, all its width.
  TEST_ASSERT_TRUE(s.display.show(changed(0, 240, 0, 100)));

  TEST_ASSERT_EQUAL(1, s.bus.count(0x10));
  TEST_ASSERT_EQUAL(0, s.bus.count(0x91));
}

void test_a_full_refresh_follows_every_so_many_partial_ones() {
  FakeBus bus;
  Epd7in5V2 panel(bus);
  RecordingLog log;
  EpdDisplay display(panel, log, PartialPolicy{25, 2});
  display.show(kFrame);
  bus.events.clear();

  display.show(changed(0, 1, 0, 1));
  display.show(changed(1, 1, 0, 1));
  TEST_ASSERT_EQUAL(2, bus.count(0x91));
  TEST_ASSERT_EQUAL(0, bus.count(0x10));
  display.show(changed(2, 1, 0, 1));
  TEST_ASSERT_EQUAL(2, bus.count(0x91));
  TEST_ASSERT_EQUAL(1, bus.count(0x10));
  // The count starts over after the full one.
  display.show(changed(3, 1, 0, 1));
  TEST_ASSERT_EQUAL(3, bus.count(0x91));
}

void test_a_failed_partial_refresh_makes_the_next_frame_a_full_one() {
  Screen s;
  s.display.show(kFrame);
  s.bus.hangAfter = 0x12;

  TEST_ASSERT_FALSE(s.display.show(changed(0, 1, 0, 1)));
  TEST_ASSERT_TRUE(s.log.has(LogLevel::Error, "after a partial refresh"));

  s.bus.hangAfter.reset();
  s.bus.stuck = false;
  s.bus.events.clear();
  TEST_ASSERT_TRUE(s.display.show(changed(0, 1, 0, 1)));
  TEST_ASSERT_EQUAL(1, s.bus.count(0x10));
  TEST_ASSERT_EQUAL(0, s.bus.count(0x91));
}

}  // namespace

void runEpdDisplayTests() {
  RUN_TEST(test_the_first_frame_is_drawn_in_full);
  RUN_TEST(test_a_small_change_is_drawn_as_a_partial_refresh_of_its_window);
  RUN_TEST(test_the_same_frame_again_touches_nothing);
  RUN_TEST(test_a_big_change_is_drawn_in_full);
  RUN_TEST(test_a_full_refresh_follows_every_so_many_partial_ones);
  RUN_TEST(test_a_failed_partial_refresh_makes_the_next_frame_a_full_one);
  RUN_TEST(test_a_frame_wakes_the_panel_draws_it_and_puts_it_to_sleep);
  RUN_TEST(test_a_frame_of_the_wrong_size_is_refused_without_touching_the_panel);
  RUN_TEST(test_a_missing_panel_is_reported_and_nothing_is_drawn);
  RUN_TEST(test_a_refresh_that_never_finishes_is_reported);
  RUN_TEST(test_a_panel_that_wont_sleep_is_reported);
}
