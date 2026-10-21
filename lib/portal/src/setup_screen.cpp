#include "setup_screen.h"

#include <cstring>

namespace countdown {

namespace {

constexpr char kMagic[] = {'S', 'E', 'T', '1'};
constexpr size_t kPasswordOffset = sizeof(kMagic);
constexpr size_t kPasswordBytes = 64;
constexpr size_t kLengthOffset = kPasswordOffset + kPasswordBytes;
static_assert(kLengthOffset + 4 == kSetupHeaderBytes, "the header is the magic, the password and the length");

// WPA2's limits on a passphrase.
constexpr size_t kMinPassword = 8;
constexpr size_t kMaxPassword = 63;

}  // namespace

std::optional<SetupHeader> parseSetupHeader(const uint8_t* header, size_t partitionBytes) {
  if (partitionBytes < kSetupHeaderBytes || std::memcmp(header, kMagic, sizeof(kMagic)) != 0) {
    return std::nullopt;
  }
  std::string password;
  for (size_t i = kPasswordOffset; i < kLengthOffset && header[i] != 0; ++i) {
    if (header[i] < 0x20 || header[i] > 0x7e) {
      return std::nullopt;
    }
    password += static_cast<char>(header[i]);
  }
  if (password.size() < kMinPassword || password.size() > kMaxPassword) {
    return std::nullopt;
  }
  const size_t frameBytes = static_cast<uint32_t>(header[kLengthOffset]) |
                            static_cast<uint32_t>(header[kLengthOffset + 1]) << 8 |
                            static_cast<uint32_t>(header[kLengthOffset + 2]) << 16 |
                            static_cast<uint32_t>(header[kLengthOffset + 3]) << 24;
  if (frameBytes == 0 || frameBytes > partitionBytes - kSetupHeaderBytes) {
    return std::nullopt;
  }
  return SetupHeader{password, frameBytes};
}

}  // namespace countdown
