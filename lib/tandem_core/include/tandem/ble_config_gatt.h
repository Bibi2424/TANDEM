#pragma once

#include <cstddef>
#include <cstdint>

#include "tandem/config_protocol.h"

namespace tandem::config {

/// Custom 128-bit GATT Service UUID for T.A.N.D.E.M. wireless configuration.
inline constexpr char kBleConfigServiceUuid[] = "7a4e0001-6f72-4d61-9c2a-54414e44454d";

/// Write / Write-Without-Response characteristic for `$TANDEM <cmd>\n` frames.
inline constexpr char kBleConfigCommandCharUuid[] = "7a4e0002-6f72-4d61-9c2a-54414e44454d";

/// Notify characteristic for `@TANDEM {...}\n` command responses.
inline constexpr char kBleConfigResponseCharUuid[] = "7a4e0003-6f72-4d61-9c2a-54414e44454d";

/// Notify characteristic for `@TANDEM {"v":1,"cmd":"live",...}\n` telemetry frames.
inline constexpr char kBleConfigLiveCharUuid[] = "7a4e0004-6f72-4d61-9c2a-54414e44454d";

/// Minimum ATT payload (23-byte MTU minus 3-byte ATT header).
inline constexpr size_t kBleMinChunkBytes = 20;

/// Maximum chunk size used when sending BLE GATT notifications.
inline constexpr size_t kBleMaxChunkBytes = 240;

/// Fixed-capacity line assembler for chunked BLE GATT writes (no heap allocation).
class BleLineAssembler {
 public:
  enum class FeedResult : uint8_t {
    Incomplete = 0,
    LineReady = 1,
    Overflow = 2,
  };

  /// Resets the internal line buffer.
  void reset() noexcept;

  /// Appends `len` bytes from `data`. Returns `LineReady` when `\n` is seen
  /// (with `line_out` populated and `\r` stripped), `Overflow` if the line
  /// exceeded `kMaxCommandLength - 1`, or `Incomplete` if more bytes are needed.
  FeedResult push_byte(uint8_t byte, char* line_out, size_t line_out_size) noexcept;

 private:
  char buf_[kMaxCommandLength]{};
  size_t len_{0};
  bool discarding_{false};
};

/// Computes a safe ATT notification payload size from a negotiated BLE MTU.
constexpr size_t clamp_ble_chunk_bytes(uint16_t mtu) noexcept {
  if (mtu <= 3) {
    return kBleMinChunkBytes;
  }
  const size_t payload = static_cast<size_t>(mtu - 3);
  if (payload < kBleMinChunkBytes) {
    return kBleMinChunkBytes;
  }
  if (payload > kBleMaxChunkBytes) {
    return kBleMaxChunkBytes;
  }
  return payload;
}

/// Invokes `fn(chunk_ptr, chunk_len)` for each slice of `data[0..len)` up to `chunk_bytes`.
template <typename Callback>
void for_each_ble_chunk(const char* data, size_t len, size_t chunk_bytes, Callback&& fn) {
  if (data == nullptr || len == 0) {
    return;
  }
  const size_t step = (chunk_bytes == 0) ? kBleMinChunkBytes : chunk_bytes;
  size_t offset = 0;
  while (offset < len) {
    const size_t remaining = len - offset;
    const size_t slice = (remaining < step) ? remaining : step;
    fn(reinterpret_cast<const uint8_t*>(data + offset), slice);
    offset += slice;
  }
}

}  // namespace tandem::config
