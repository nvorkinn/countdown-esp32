#include <unity.h>

#include <string>

#include "fakes.h"
#include "token.h"

using namespace countdown;
using namespace countdown::testing;

namespace {

std::string firstHashInput;
std::string secondHashInput;

std::string testSha256Hex(const std::string& input) {
  if (firstHashInput.empty()) {
    firstHashInput = input;
    return "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  }
  secondHashInput = input;
  return "3f227893a4345ed8dee4d8ff20104535c116c88b863d527539608ba12c7b46ac";
}

std::string encode(const std::string& text) {
  return base64UrlEncode(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

void test_base64url_matches_the_rfc_4648_vectors_without_padding() {
  TEST_ASSERT_EQUAL_STD_STRING("", encode(""));
  TEST_ASSERT_EQUAL_STD_STRING("Zg", encode("f"));
  TEST_ASSERT_EQUAL_STD_STRING("Zm8", encode("fo"));
  TEST_ASSERT_EQUAL_STD_STRING("Zm9v", encode("foo"));
  TEST_ASSERT_EQUAL_STD_STRING("Zm9vYg", encode("foob"));
  TEST_ASSERT_EQUAL_STD_STRING("Zm9vYmE", encode("fooba"));
  TEST_ASSERT_EQUAL_STD_STRING("Zm9vYmFy", encode("foobar"));
}

void test_base64url_uses_dash_and_underscore() {
  const uint8_t bytes[] = {0xFB, 0xFF, 0xBF};  // "+/+/" in standard base64
  TEST_ASSERT_EQUAL_STD_STRING("-_-_", base64UrlEncode(bytes, sizeof bytes));
}

void test_a_token_is_24_random_bytes_like_token_urlsafe() {
  CountingRandom random;

  const std::string token = tokenUrlsafe(random);

  TEST_ASSERT_EQUAL_STD_STRING(kFirstToken, token);
  TEST_ASSERT_EQUAL(32, token.size());
  TEST_ASSERT_EQUAL(24, random.next);
}

void test_each_token_takes_fresh_bytes() {
  CountingRandom random;

  tokenUrlsafe(random);

  TEST_ASSERT_EQUAL_STD_STRING(kSecondToken, tokenUrlsafe(random));
}

void test_pairing_token_hashes_secret_then_prefixed_lowercase_hex_digest() {
  firstHashInput.clear();
  secondHashInput.clear();

  const std::string token = pairingToken("abc", testSha256Hex);

  TEST_ASSERT_EQUAL_STD_STRING("abc", firstHashInput);
  TEST_ASSERT_EQUAL_STD_STRING(
      "pairing:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", secondHashInput);
  TEST_ASSERT_EQUAL_STD_STRING("3f227893a4345ed8dee4d8ff20104535c116c88b863d527539608ba12c7b46ac", token);
}

}  // namespace

void runTokenTests() {
  RUN_TEST(test_base64url_matches_the_rfc_4648_vectors_without_padding);
  RUN_TEST(test_base64url_uses_dash_and_underscore);
  RUN_TEST(test_a_token_is_24_random_bytes_like_token_urlsafe);
  RUN_TEST(test_each_token_takes_fresh_bytes);
  RUN_TEST(test_pairing_token_hashes_secret_then_prefixed_lowercase_hex_digest);
}
