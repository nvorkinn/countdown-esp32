#include <unity.h>

#include <string>
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

// Memory for at most 100 bytes.
bool fitsIn100Bytes(size_t bytes) { return bytes <= 100; }

void test_response_body_reports_a_buffer_that_would_not_fit_in_memory() {
  std::vector<uint8_t> bytes;
  ResponseBody body(bytes, 48000, fitsIn100Bytes);
  TEST_ASSERT_TRUE(body.reserve(100));
  TEST_ASSERT_FALSE(body.reserve(101));
  TEST_ASSERT_EQUAL_STRING("Not enough memory for the HTTP response", body.error());
}

void test_response_body_reports_growth_that_would_not_fit_in_memory() {
  std::vector<uint8_t> bytes;
  ResponseBody body(bytes, 48000, fitsIn100Bytes);
  const std::vector<uint8_t> chunk(60, 0x5A);
  TEST_ASSERT_EQUAL(60, body.append(chunk.data(), chunk.size()));
  TEST_ASSERT_EQUAL(0, body.append(chunk.data(), chunk.size()));  // 120 bytes: more than fit
  TEST_ASSERT_EQUAL(60, bytes.size());
  TEST_ASSERT_EQUAL_STRING("Not enough memory for the HTTP response", body.error());
  TEST_ASSERT_EQUAL(0, body.append(chunk.data(), 1));  // and it stays failed
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

// The broker's 304s come through Caddy with no Content-Length (and the connection closed), so reading
// a body there failed the poll.
void test_responses_without_a_body() {
  for (const int status : {100, 101, 103, 204, 304}) {
    TEST_ASSERT_FALSE_MESSAGE(responseHasBody(status), std::to_string(status).c_str());
  }
}

void test_responses_with_a_body() {
  for (const int status : {200, 201, 202, 400, 401, 404, 409, 429, 500, 503}) {
    TEST_ASSERT_TRUE_MESSAGE(responseHasBody(status), std::to_string(status).c_str());
  }
}

}  // namespace

void runResponseBodyTests() {
  RUN_TEST(test_response_body_accepts_an_entire_7in8_frame);
  RUN_TEST(test_response_body_bounds_chunked_growth_and_stops_after_failure);
  RUN_TEST(test_response_body_refuses_an_oversized_content_length);
  RUN_TEST(test_response_body_reports_a_buffer_that_would_not_fit_in_memory);
  RUN_TEST(test_response_body_reports_growth_that_would_not_fit_in_memory);
  RUN_TEST(test_response_body_handles_empty_and_exact_limit_responses);
  RUN_TEST(test_responses_without_a_body);
  RUN_TEST(test_responses_with_a_body);
}
