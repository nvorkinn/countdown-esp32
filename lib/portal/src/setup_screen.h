#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace countdown {

// The header provision/provision.py writes at the start of the `setup` partition: "SET1", the set-up
// network's password (64 bytes, NUL-padded) and the length of the frame that follows it (4 bytes,
// little-endian). The frame is the screen shown while the set-up network is up.
constexpr size_t kSetupHeaderBytes = 72;

struct SetupHeader {
  std::string password;
  size_t frameBytes;
};

// Reads the header from the first kSetupHeaderBytes of a `setup` partition `partitionBytes` long.
// Nothing if it isn't one (a partition that was never written reads as 0xFF), if the password isn't
// one WPA2 takes (8 to 63 printable characters), or if the frame wouldn't fit in the partition.
std::optional<SetupHeader> parseSetupHeader(const uint8_t* header, size_t partitionBytes);

}  // namespace countdown
