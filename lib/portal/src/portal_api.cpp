#include "portal_api.h"

#include <algorithm>
#include <map>

namespace countdown {

std::string jsonEscape(const std::string& text) {
  std::string result;
  result.reserve(text.size() + 2);
  for (const char c : text) {
    switch (c) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          constexpr char hex[] = "0123456789abcdef";
          result += "\\u00";
          result += hex[static_cast<unsigned char>(c) >> 4];
          result += hex[c & 0x0f];
        } else {
          result += c;  // UTF-8 goes through as it is
        }
    }
  }
  return result;
}

std::string networksJson(const std::vector<ScannedNetwork>& networks) {
  std::map<std::string, ScannedNetwork> strongest;
  for (const ScannedNetwork& network : networks) {
    if (network.ssid.empty()) {
      continue;
    }
    const auto found = strongest.find(network.ssid);
    if (found == strongest.end() || network.rssi > found->second.rssi) {
      strongest[network.ssid] = network;
    }
  }
  std::vector<ScannedNetwork> sorted;
  sorted.reserve(strongest.size());
  for (const auto& entry : strongest) {
    sorted.push_back(entry.second);
  }
  // Strongest first; the map's order (by name) breaks ties, so the list is stable between scans.
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const ScannedNetwork& a, const ScannedNetwork& b) { return a.rssi > b.rssi; });
  std::string json = "[";
  for (const ScannedNetwork& network : sorted) {
    if (json.size() > 1) {
      json += ",";
    }
    json += "{\"ssid\":\"" + jsonEscape(network.ssid) + "\",\"rssi\":" + std::to_string(network.rssi) +
            ",\"secure\":" + (network.secure ? "true" : "false") + "}";
  }
  return json + "]";
}

std::string credentialsProblem(const std::string& ssid, const std::string& password) {
  if (ssid.empty()) {
    return "Enter the network's name";
  }
  if (ssid.size() > 32) {
    return "A network's name is at most 32 characters";
  }
  if (password.size() > 63) {
    return "A password is at most 63 characters";
  }
  return "";
}

void ConnectAttempt::start(unsigned long nowMs) {
  startedMs_ = nowMs;
  state_ = State::Connecting;
}

ConnectAttempt::State ConnectAttempt::update(unsigned long nowMs, bool linkUp) {
  if (state_ == State::Connecting) {
    if (linkUp) {
      state_ = State::Connected;
    } else if (nowMs - startedMs_ >= timeoutMs_) {
      state_ = State::Failed;
    }
  }
  return state_;
}

const char* stateName(ConnectAttempt::State state) {
  switch (state) {
    case ConnectAttempt::State::Connecting:
      return "connecting";
    case ConnectAttempt::State::Connected:
      return "connected";
    case ConnectAttempt::State::Failed:
      return "failed";
    default:
      return "idle";
  }
}

}  // namespace countdown
