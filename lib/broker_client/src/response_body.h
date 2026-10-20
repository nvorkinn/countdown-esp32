#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

namespace countdown {

// Whether a response to a POST has a body to read. 1xx, 204 and 304 responses never do (RFC 9110
// sections 6.4.1 and 15.4.5), even with no Content-Length, and the server may close the connection
// straight after one, so reading would only fail (or wait for the server to time out).
inline bool responseHasBody(int status) { return status >= 200 && status != 204 && status != 304; }

// Bounds chunked as well as fixed-length responses, and leaves allocation failures reportable.
class ResponseBody {
 public:
  ResponseBody(std::vector<uint8_t>& bytes, size_t limit) : bytes_(bytes), limit_(limit) {}

  bool reserve(size_t size) {
    if (size > limit_) {
      error_ = "HTTP response exceeds the configured body limit";
      return false;
    }
    try {
      bytes_.reserve(size);
    } catch (const std::bad_alloc&) {
      error_ = "Not enough memory for the HTTP response";
      return false;
    }
    return true;
  }

  size_t append(const uint8_t* bytes, size_t size) {
    if (error_ || size > limit_ - bytes_.size()) {
      if (!error_) {
        error_ = "HTTP response exceeds the configured body limit";
      }
      return 0;
    }
    const size_t needed = bytes_.size() + size;
    if (needed > bytes_.capacity()) {
      const size_t capacity = bytes_.capacity();
      const size_t growth = std::min(capacity, limit_ - capacity);
      if (!reserve(std::max(needed, capacity + growth))) {
        return 0;
      }
    }
    bytes_.insert(bytes_.end(), bytes, bytes + size);
    return size;
  }

  const char* error() const { return error_; }

 private:
  std::vector<uint8_t>& bytes_;
  size_t limit_;
  const char* error_ = nullptr;
};

}  // namespace countdown
