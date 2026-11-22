#pragma once

#include <cstddef>
#include <cstdint>

namespace tandem {

/// Formats `<prefix>-XXXX` into `out` using the uppercase hex of `mac[4]` and `mac[5]`.
/// Returns the number of characters written (excluding null terminator), or 0 on invalid args.
size_t format_device_name(char* out,
                          size_t out_len,
                          const char* prefix,
                          const std::uint8_t mac[6]) noexcept;

}  // namespace tandem
