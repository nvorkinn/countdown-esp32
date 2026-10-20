#include "frame_diff.h"

namespace countdown {

std::optional<Window> changedWindow(const uint8_t* before, const uint8_t* after, int width, int height) {
  const int rowBytes = (width + 7) / 8;
  int firstRow = height, lastRow = -1, firstByte = rowBytes, lastByte = -1;
  for (int y = 0; y < height; ++y) {
    const size_t offset = static_cast<size_t>(y) * rowBytes;
    for (int i = 0; i < rowBytes; ++i) {
      if (before[offset + i] != after[offset + i]) {
        firstRow = y < firstRow ? y : firstRow;
        lastRow = y;
        firstByte = i < firstByte ? i : firstByte;
        lastByte = i > lastByte ? i : lastByte;
      }
    }
  }
  if (lastRow < 0) {
    return std::nullopt;
  }
  return Window{firstByte * 8, firstRow, (lastByte - firstByte + 1) * 8, lastRow - firstRow + 1};
}

}  // namespace countdown
