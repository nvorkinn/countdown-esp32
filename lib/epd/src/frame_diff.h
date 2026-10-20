#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "epd_panel.h"

namespace countdown {

// The smallest byte-aligned window that holds every difference between two frames of `width` x `height` pixels (rows
// padded to whole bytes, as the broker sends them), or nothing if they're the same.
std::optional<Window> changedWindow(const uint8_t* before, const uint8_t* after, int width, int height);

}  // namespace countdown
