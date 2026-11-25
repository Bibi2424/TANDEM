#include "tandem/ble_config_gatt.h"

#include <cstring>

namespace tandem::config {

void BleLineAssembler::reset() noexcept {
  len_ = 0;
  discarding_ = false;
  buf_[0] = '\0';
}

BleLineAssembler::FeedResult BleLineAssembler::push_byte(uint8_t byte, char* line_out, size_t line_out_size) noexcept {
  const char ch = static_cast<char>(byte);

  if (ch == '\r') {
    return FeedResult::Incomplete;
  }

  if (ch == '\n') {
    if (discarding_) {
      reset();
      return FeedResult::Overflow;
    }
    if (line_out != nullptr && line_out_size > 0) {
      const size_t copy_len = (len_ < line_out_size - 1) ? len_ : (line_out_size - 1);
      if (copy_len > 0) {
        std::memcpy(line_out, buf_, copy_len);
      }
      line_out[copy_len] = '\0';
    }
    reset();
    return FeedResult::LineReady;
  }

  if (discarding_) {
    return FeedResult::Incomplete;
  }

  if (len_ + 1 >= sizeof(buf_)) {
    discarding_ = true;
    return FeedResult::Incomplete;
  }

  buf_[len_++] = ch;
  return FeedResult::Incomplete;
}

}  // namespace tandem::config
