#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "setup_screen.h"

using countdown::kSetupHeaderBytes;
using countdown::parseSetupHeader;

namespace {

// The partition in partitions.csv: 384 KB.
constexpr size_t kPartitionBytes = 0x60000;

// A header as provision/provision.py packs it: "<4s64sI".
std::vector<uint8_t> header(const std::string& password, uint32_t frameBytes, const std::string& magic = "SET1") {
  std::vector<uint8_t> bytes(kSetupHeaderBytes, 0);
  std::copy(magic.begin(), magic.end(), bytes.begin());
  std::copy(password.begin(), password.end(), bytes.begin() + 4);
  for (size_t i = 0; i < 4; ++i) {
    bytes[68 + i] = static_cast<uint8_t>(frameBytes >> (8 * i));
  }
  return bytes;
}

void test_reads_the_password_and_frame_length() {
  const auto parsed = parseSetupHeader(header("JzMqG675qsVd", 48000).data(), kPartitionBytes);
  TEST_ASSERT_TRUE(parsed.has_value());
  TEST_ASSERT_EQUAL_STRING("JzMqG675qsVd", parsed->password.c_str());
  TEST_ASSERT_EQUAL_UINT32(48000, parsed->frameBytes);
}

void test_frame_length_is_little_endian() {
  // The IT8951's frame: 1872 / 8 * 1404 bytes.
  const auto parsed = parseSetupHeader(header("JzMqG675qsVd", 328536).data(), kPartitionBytes);
  TEST_ASSERT_TRUE(parsed.has_value());
  TEST_ASSERT_EQUAL_UINT32(328536, parsed->frameBytes);
}

void test_erased_partition() {
  const std::vector<uint8_t> erased(kSetupHeaderBytes, 0xFF);
  TEST_ASSERT_FALSE(parseSetupHeader(erased.data(), kPartitionBytes).has_value());
}

void test_other_magic() {
  TEST_ASSERT_FALSE(parseSetupHeader(header("JzMqG675qsVd", 48000, "SET2").data(), kPartitionBytes).has_value());
  TEST_ASSERT_FALSE(parseSetupHeader(header("JzMqG675qsVd", 48000, "set1").data(), kPartitionBytes).has_value());
}

void test_password_length_limits() {
  TEST_ASSERT_FALSE(parseSetupHeader(header("1234567", 48000).data(), kPartitionBytes).has_value());
  TEST_ASSERT_TRUE(parseSetupHeader(header("12345678", 48000).data(), kPartitionBytes).has_value());
  TEST_ASSERT_TRUE(parseSetupHeader(header(std::string(63, 'a'), 48000).data(), kPartitionBytes).has_value());
  // 64 characters leave no NUL: too long for WPA2.
  TEST_ASSERT_FALSE(parseSetupHeader(header(std::string(64, 'a'), 48000).data(), kPartitionBytes).has_value());
  TEST_ASSERT_FALSE(parseSetupHeader(header("", 48000).data(), kPartitionBytes).has_value());
}

void test_password_must_be_printable() {
  TEST_ASSERT_FALSE(parseSetupHeader(header("JzMqG675\nqsVd", 48000).data(), kPartitionBytes).has_value());
  TEST_ASSERT_FALSE(parseSetupHeader(header("JzMqG675\x7fqsVd", 48000).data(), kPartitionBytes).has_value());
  TEST_ASSERT_TRUE(parseSetupHeader(header("pass word ~!", 48000).data(), kPartitionBytes).has_value());
}

void test_frame_must_fit_in_the_partition() {
  TEST_ASSERT_FALSE(parseSetupHeader(header("JzMqG675qsVd", 0).data(), kPartitionBytes).has_value());
  const uint32_t room = kPartitionBytes - kSetupHeaderBytes;
  TEST_ASSERT_TRUE(parseSetupHeader(header("JzMqG675qsVd", room).data(), kPartitionBytes).has_value());
  TEST_ASSERT_FALSE(parseSetupHeader(header("JzMqG675qsVd", room + 1).data(), kPartitionBytes).has_value());
  TEST_ASSERT_FALSE(parseSetupHeader(header("JzMqG675qsVd", 0xFFFFFFFF).data(), kPartitionBytes).has_value());
}

void test_partition_smaller_than_a_header() {
  TEST_ASSERT_FALSE(parseSetupHeader(header("JzMqG675qsVd", 1).data(), kSetupHeaderBytes - 1).has_value());
}

}  // namespace

void runSetupScreenTests() {
  RUN_TEST(test_reads_the_password_and_frame_length);
  RUN_TEST(test_frame_length_is_little_endian);
  RUN_TEST(test_erased_partition);
  RUN_TEST(test_other_magic);
  RUN_TEST(test_password_length_limits);
  RUN_TEST(test_password_must_be_printable);
  RUN_TEST(test_frame_must_fit_in_the_partition);
  RUN_TEST(test_partition_smaller_than_a_header);
}
