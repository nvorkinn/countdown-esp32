#include <unity.h>

#include <vector>

#include "response_body.h"

using namespace countdown;

namespace {

void test_response_body_accepts_an_entire_7in8_frame() {
  std::vector<uint8_t> bytes;
  ResponseBody body(bytes, 328536);
  TEST_ASSERT_TRUE(body.reserve(328536));
  const std::vector<uint8_t> chunk(1024, 0xA5);
  for (int i = 0; i < 320; ++i) {
    TEST_ASSERT_EQUAL(1024, body.append(chunk.data(), chunk.size()));
  }
  TEST_ASSERT_EQUAL(856, body.append(chunk.data(), 856));
  TEST_ASSERT_EQUAL(328536, bytes.size());
  TEST_ASSERT_EQUAL_HEX8(0xA5, bytes.back());
  TEST_ASSERT_NULL(body.error());
}

void test_response_body_bounds_chunked_growth_and_stops_after_failure() {
  std::vector<uint8_t> bytes;
  ResponseBody body(bytes, 11);
  const uint8_t data[] = {1, 2, 3, 4};
  TEST_ASSERT_EQUAL(4, body.append(data, 4));
  TEST_ASSERT_EQUAL(4, body.append(data, 4));
  TEST_ASSERT_LESS_OR_EQUAL(11, bytes.capacity());
  TEST_ASSERT_EQUAL(0, body.append(data, 4));
  TEST_ASSERT_EQUAL(8, bytes.size());
  TEST_ASSERT_NOT_NULL(body.error());
  TEST_ASSERT_EQUAL(0, body.append(data, 1));
}

void test_response_body_refuses_an_oversized_content_length() {
  std::vector<uint8_t> bytes;
  ResponseBody body(bytes, 48000);
  TEST_ASSERT_FALSE(body.reserve(328536));
  TEST_ASSERT_EQUAL(0, bytes.capacity());
  TEST_ASSERT_NOT_NULL(body.error());
}

void test_response_body_handles_empty_and_exact_limit_responses() {
  std::vector<uint8_t> bytes;
  ResponseBody body(bytes, 1);
  const uint8_t byte = 0x80;
  TEST_ASSERT_TRUE(body.reserve(0));
  TEST_ASSERT_EQUAL(0, body.append(&byte, 0));
  TEST_ASSERT_EQUAL(1, body.append(&byte, 1));
  TEST_ASSERT_NULL(body.error());
  TEST_ASSERT_EQUAL(0, body.append(&byte, 1));
  TEST_ASSERT_EQUAL(1, bytes.size());
}

}  // namespace

void runResponseBodyTests() {
  RUN_TEST(test_response_body_accepts_an_entire_7in8_frame);
  RUN_TEST(test_response_body_bounds_chunked_growth_and_stops_after_failure);
  RUN_TEST(test_response_body_refuses_an_oversized_content_length);
  RUN_TEST(test_response_body_handles_empty_and_exact_limit_responses);
}
