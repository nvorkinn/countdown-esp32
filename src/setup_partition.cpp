#include "setup_partition.h"

#include <esp_partition.h>

#include <utility>

#include "setup_screen.h"

namespace {

// The `setup` row of partitions.csv: type 0x40, the first of the types left for applications.
const auto kType = static_cast<esp_partition_type_t>(0x40);
const auto kSubtype = static_cast<esp_partition_subtype_t>(0x00);
constexpr char kLabel[] = "setup";

}  // namespace

std::optional<SetupScreen> readSetupScreen(countdown::Log& log) {
  const esp_partition_t* partition = esp_partition_find_first(kType, kSubtype, kLabel);
  if (partition == nullptr) {
    log.warning("The partition table has no setup partition, so there's no set-up screen");
    return std::nullopt;
  }
  uint8_t header[countdown::kSetupHeaderBytes];
  if (esp_partition_read(partition, 0, header, sizeof(header)) != ESP_OK) {
    log.warning("Couldn't read the setup partition");
    return std::nullopt;
  }
  const auto parsed = countdown::parseSetupHeader(header, partition->size);
  if (!parsed) {
    log.warning("The setup partition holds no set-up screen: provision the board with provision/provision.py");
    return std::nullopt;
  }
  std::vector<uint8_t> frame(parsed->frameBytes);
  if (esp_partition_read(partition, countdown::kSetupHeaderBytes, frame.data(), frame.size()) != ESP_OK) {
    log.warning("Couldn't read the set-up screen from the setup partition");
    return std::nullopt;
  }
  return SetupScreen{parsed->password, std::move(frame)};
}
