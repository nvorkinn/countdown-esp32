#pragma once

#include <optional>
#include <string>

namespace countdown {

// How long to wait when the broker doesn't say (no Retry-After) or can't be reached.
constexpr int kDefaultRetryS = 60;
// So a stalled connection fails (and is retried) instead of hanging the wait.
constexpr int kRequestTimeoutS = 10;

// The longest a Retry-After is waited. The broker asks for a minute at most, so anything much longer is a
// mistake, and a board can't be woken from a wait but by cutting its power.
constexpr int kMaxRetryS = 60 * 60;

// The Retry-After header's delay in seconds, kMaxRetryS at most, or kDefaultRetryS when it's missing or
// isn't a whole number of seconds (an HTTP date, say).
int retryAfterSeconds(const std::optional<std::string>& header);

}  // namespace countdown
