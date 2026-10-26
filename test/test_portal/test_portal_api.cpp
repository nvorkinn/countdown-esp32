#include <unity.h>

#include <limits>
#include <string>
#include <vector>

#include "portal_api.h"

using countdown::ConnectAttempt;
using countdown::credentialsProblem;
using countdown::jsonEscape;
using countdown::networksJson;
using countdown::ScannedNetwork;
using countdown::stateName;

namespace {

using State = ConnectAttempt::State;

void test_escape_leaves_plain_text_alone() { TEST_ASSERT_EQUAL_STRING("Home 5G", jsonEscape("Home 5G").c_str()); }

void test_escape_quotes_and_backslashes() {
  TEST_ASSERT_EQUAL_STRING("say \\\"hi\\\" \\\\ bye", jsonEscape("say \"hi\" \\ bye").c_str());
}

void test_escape_control_characters() {
  TEST_ASSERT_EQUAL_STRING("a\\nb\\rc\\td\\u0001e", jsonEscape("a\nb\rc\td\x01" "e").c_str());
}

void test_escape_keeps_utf8() { TEST_ASSERT_EQUAL_STRING("Café ☕", jsonEscape("Café ☕").c_str()); }

void test_no_networks() { TEST_ASSERT_EQUAL_STRING("[]", networksJson({}).c_str()); }

void test_networks_strongest_first() {
  const std::vector<ScannedNetwork> networks = {{"Weak", -80, true}, {"Strong", -40, false}, {"Middle", -60, true}};
  TEST_ASSERT_EQUAL_STRING(
      "[{\"ssid\":\"Strong\",\"rssi\":-40,\"secure\":false},"
      "{\"ssid\":\"Middle\",\"rssi\":-60,\"secure\":true},"
      "{\"ssid\":\"Weak\",\"rssi\":-80,\"secure\":true}]",
      networksJson(networks).c_str());
}

void test_networks_one_entry_per_name() {
  // A router's two bands, or an extender: the strongest one stands for the name.
  const std::vector<ScannedNetwork> networks = {{"Home", -70, true}, {"Home", -45, true}, {"Home", -60, true}};
  TEST_ASSERT_EQUAL_STRING("[{\"ssid\":\"Home\",\"rssi\":-45,\"secure\":true}]", networksJson(networks).c_str());
}

void test_networks_ties_by_name() {
  const std::vector<ScannedNetwork> networks = {{"b", -50, true}, {"a", -50, true}};
  TEST_ASSERT_EQUAL_STRING("[{\"ssid\":\"a\",\"rssi\":-50,\"secure\":true},{\"ssid\":\"b\",\"rssi\":-50,\"secure\":true}]",
                           networksJson(networks).c_str());
}

void test_networks_leave_out_hidden_ones() {
  const std::vector<ScannedNetwork> networks = {{"", -30, true}, {"Shown", -50, true}};
  TEST_ASSERT_EQUAL_STRING("[{\"ssid\":\"Shown\",\"rssi\":-50,\"secure\":true}]", networksJson(networks).c_str());
}

void test_networks_escape_names() {
  const std::vector<ScannedNetwork> networks = {{"Bob's \"Net\"", -50, true}};
  TEST_ASSERT_EQUAL_STRING("[{\"ssid\":\"Bob's \\\"Net\\\"\",\"rssi\":-50,\"secure\":true}]",
                           networksJson(networks).c_str());
}

void test_credentials_accepted() {
  TEST_ASSERT_EQUAL_STRING("", credentialsProblem("Home", "hunter22").c_str());
  TEST_ASSERT_EQUAL_STRING("", credentialsProblem("Open network", "").c_str());
  TEST_ASSERT_EQUAL_STRING("", credentialsProblem(std::string(32, 'n'), std::string(63, 'p')).c_str());
}

void test_credentials_need_a_name() { TEST_ASSERT_EQUAL_STRING("Enter the network's name", credentialsProblem("", "x").c_str()); }

void test_credentials_length_limits() {
  TEST_ASSERT_EQUAL_STRING("A network's name is at most 32 characters", credentialsProblem(std::string(33, 'n'), "").c_str());
  TEST_ASSERT_EQUAL_STRING("A password is at most 63 characters", credentialsProblem("Home", std::string(64, 'p')).c_str());
}

void test_attempt_starts_idle() {
  ConnectAttempt attempt(1000);
  TEST_ASSERT_EQUAL(State::Idle, attempt.state());
  TEST_ASSERT_EQUAL(State::Idle, attempt.update(5000, true));  // nothing started: the link is someone else's
}

void test_attempt_connects_when_the_link_comes_up() {
  ConnectAttempt attempt(1000);
  attempt.start(100);
  TEST_ASSERT_EQUAL(State::Connecting, attempt.update(200, false));
  TEST_ASSERT_EQUAL(State::Connected, attempt.update(300, true));
  TEST_ASSERT_EQUAL(State::Connected, attempt.update(400, false));  // a blip doesn't undo it
}

void test_attempt_waits_out_the_timeout_before_failing() {
  ConnectAttempt attempt(1000);
  attempt.start(100);
  TEST_ASSERT_EQUAL(State::Connecting, attempt.update(1099, false));
  TEST_ASSERT_EQUAL(State::Failed, attempt.update(1100, false));
  TEST_ASSERT_EQUAL(State::Failed, attempt.update(1200, true));  // too late
}

void test_attempt_survives_millis_wrapping() {
  // Started 16 ms before millis() wraps (32-bit on the board, wider here: either way the top).
  ConnectAttempt attempt(1000);
  attempt.start(std::numeric_limits<unsigned long>::max() - 15);
  TEST_ASSERT_EQUAL(State::Connecting, attempt.update(983, false));  // 999 ms in
  TEST_ASSERT_EQUAL(State::Failed, attempt.update(984, false));      // 1000 ms in
}

void test_attempt_reset_allows_another() {
  ConnectAttempt attempt(1000);
  attempt.start(0);
  attempt.update(1000, false);
  TEST_ASSERT_EQUAL(State::Failed, attempt.state());
  attempt.reset();
  TEST_ASSERT_EQUAL(State::Idle, attempt.state());
  attempt.start(2000);
  TEST_ASSERT_EQUAL(State::Connected, attempt.update(2001, true));
}

void test_state_names() {
  TEST_ASSERT_EQUAL_STRING("idle", stateName(State::Idle));
  TEST_ASSERT_EQUAL_STRING("connecting", stateName(State::Connecting));
  TEST_ASSERT_EQUAL_STRING("connected", stateName(State::Connected));
  TEST_ASSERT_EQUAL_STRING("failed", stateName(State::Failed));
}

}  // namespace

void runPortalApiTests() {
  RUN_TEST(test_escape_leaves_plain_text_alone);
  RUN_TEST(test_escape_quotes_and_backslashes);
  RUN_TEST(test_escape_control_characters);
  RUN_TEST(test_escape_keeps_utf8);
  RUN_TEST(test_no_networks);
  RUN_TEST(test_networks_strongest_first);
  RUN_TEST(test_networks_one_entry_per_name);
  RUN_TEST(test_networks_ties_by_name);
  RUN_TEST(test_networks_leave_out_hidden_ones);
  RUN_TEST(test_networks_escape_names);
  RUN_TEST(test_credentials_accepted);
  RUN_TEST(test_credentials_need_a_name);
  RUN_TEST(test_credentials_length_limits);
  RUN_TEST(test_attempt_starts_idle);
  RUN_TEST(test_attempt_connects_when_the_link_comes_up);
  RUN_TEST(test_attempt_waits_out_the_timeout_before_failing);
  RUN_TEST(test_attempt_survives_millis_wrapping);
  RUN_TEST(test_attempt_reset_allows_another);
  RUN_TEST(test_state_names);
}
