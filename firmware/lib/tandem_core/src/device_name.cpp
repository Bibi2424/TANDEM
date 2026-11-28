// SPDX-License-Identifier: GPL-3.0-only
#include "tandem/device_name.h"

#include <cstdio>

namespace tandem {

size_t format_device_name(char* out, size_t out_len, const char* prefix, const std::uint8_t mac[6]) noexcept {
  if (out == nullptr || out_len == 0) {
    return 0;
  }
  if (prefix == nullptr || mac == nullptr) {
    out[0] = '\0';
    return 0;
  }

  const int written = std::snprintf(out, out_len, "%s-%02X%02X", prefix, mac[4], mac[5]);
  if (written < 0) {
    out[0] = '\0';
    return 0;
  }
  const size_t uw = static_cast<size_t>(written);
  return (uw >= out_len) ? (out_len - 1U) : uw;
}

}  // namespace tandem
