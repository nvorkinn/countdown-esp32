#pragma once

#include <string>
#include <vector>

namespace countdown {

// A network the set-up portal's scan found.
struct ScannedNetwork {
  std::string ssid;
  int rssi;
  bool secure;
};

// `text` as it goes between double quotes in JSON.
std::string jsonEscape(const std::string& text);

// The list the set-up page shows, as a JSON array of {"ssid", "rssi", "secure"}: one entry per
// network name, the strongest of each (a router's two bands and its extenders all show up),
// strongest first, and no hidden networks (an empty name); the page has a field for those.
std::string networksJson(const std::vector<ScannedNetwork>& networks);

// What's wrong with the credentials the page sent, before the Wi-Fi driver sees them: the empty
// string if nothing. A name is 1 to 32 bytes; a password up to 63, or none for an open network.
std::string credentialsProblem(const std::string& ssid, const std::string& password);

// One attempt to join a network from the set-up page. The driver's status alone can't fail it:
// straight after boot it often says "network not found" before its first scan has seen the
// router, while a few seconds later it would have joined. So the attempt only fails once the
// whole timeout has passed without the link coming up, as join() in src/wifi_setup.cpp does.
class ConnectAttempt {
 public:
  enum class State { Idle, Connecting, Connected, Failed };

  explicit ConnectAttempt(unsigned long timeoutMs) : timeoutMs_(timeoutMs) {}

  void start(unsigned long nowMs);
  // With the time and whether the link is up now; Connected and Failed are final until reset().
  State update(unsigned long nowMs, bool linkUp);
  State state() const { return state_; }
  void reset() { state_ = State::Idle; }

 private:
  unsigned long timeoutMs_;
  unsigned long startedMs_ = 0;
  State state_ = State::Idle;
};

// "idle", "connecting", "connected" or "failed": the page's names for the states.
const char* stateName(ConnectAttempt::State state);

}  // namespace countdown
