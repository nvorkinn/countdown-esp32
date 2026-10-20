#include <unity.h>

#include <vector>

#include "frame_diff.h"

using namespace countdown;

namespace {

// 32x4 pixels: 4 bytes a row.
std::vector<uint8_t> blank() { return std::vector<uint8_t>(16, 0xFF); }

void test_identical_frames_have_no_window() {
  const std::vector<uint8_t> a = blank();
  TEST_ASSERT_FALSE(changedWindow(a.data(), a.data(), 32, 4).has_value());
}

void test_the_window_is_the_bounding_box_in_whole_bytes() {
  const std::vector<uint8_t> a = blank();
  std::vector<uint8_t> b = blank();
  b[1 * 4 + 1] = 0x00;
  b[2 * 4 + 2] = 0x7F;

  const auto window = changedWindow(a.data(), b.data(), 32, 4);

  TEST_ASSERT_TRUE(window.has_value());
  TEST_ASSERT_EQUAL(8, window->x);
  TEST_ASSERT_EQUAL(1, window->y);
  TEST_ASSERT_EQUAL(16, window->width);
  TEST_ASSERT_EQUAL(2, window->height);
}

void test_a_change_in_the_last_byte_of_the_last_row() {
  const std::vector<uint8_t> a = blank();
  std::vector<uint8_t> b = blank();
  b[15] = 0xFE;

  const auto window = changedWindow(a.data(), b.data(), 32, 4);

  TEST_ASSERT_TRUE(window.has_value());
  TEST_ASSERT_EQUAL(24, window->x);
  TEST_ASSERT_EQUAL(3, window->y);
  TEST_ASSERT_EQUAL(8, window->width);
  TEST_ASSERT_EQUAL(1, window->height);
}

}  // namespace

void runFrameDiffTests() {
  RUN_TEST(test_identical_frames_have_no_window);
  RUN_TEST(test_the_window_is_the_bounding_box_in_whole_bytes);
  RUN_TEST(test_a_change_in_the_last_byte_of_the_last_row);
}
