#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ports.h"

// What provision/provision.py wrote to this board: the set-up network's password, and the frame that
// shows how to join it.
struct SetupScreen {
  std::string password;
  std::vector<uint8_t> frame;
};

// Reads the set-up screen from the `setup` partition. Nothing, with a warning, if the board was never
// provisioned.
std::optional<SetupScreen> readSetupScreen(countdown::Log& log);
