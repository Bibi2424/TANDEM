#pragma once

#include <cstddef>
#include <cstdint>

#include "tandem/input_pipeline.h"
#include "tandem/types.h"

namespace tandem::config {

inline constexpr uint8_t kProtocolVersion = 1;
inline constexpr char kCommandPrefix[] = "$TANDEM ";
inline constexpr char kResponsePrefix[] = "@TANDEM ";
inline constexpr size_t kMaxCommandLength = 160;
inline constexpr size_t kMaxResponseLength = 1024;

struct DeviceCapabilities {
  const char* board{"UNKNOWN"};
  StickId stick_id{StickId::Left};
  const char* transport_mode{"UNKNOWN"};
  bool synthetic{false};
  uint8_t axes_mask{0};
  uint32_t buttons_mask{0};
  bool has_hat{false};
  int32_t adc_max{kDefaultAxisMax};
};

struct ConfigCallbacks {
  bool (*on_save)(void* ctx){nullptr};
  bool (*on_reset)(void* ctx){nullptr};
  void (*on_live_changed)(void* ctx, bool enabled){nullptr};
  void* ctx{nullptr};
};

/// Formats a `caps` response line (`@TANDEM {...}\n`). Returns bytes written, or 0 on overflow.
size_t format_caps_response(const DeviceCapabilities& caps,
                            char* out,
                            size_t out_size) noexcept;

/// Formats a `get` response line (`@TANDEM {...}\n`). Returns bytes written, or 0 on overflow.
size_t format_get_response(const StickInputConfig& cfg,
                           char* out,
                           size_t out_size) noexcept;

/// Formats a `live` sample frame (`@TANDEM {...}\n`). Returns bytes written, or 0 on overflow.
size_t format_live_response(const RawStickInput& raw,
                            const StickState& state,
                            bool synthetic,
                            char* out,
                            size_t out_size) noexcept;

/// Parses and executes a config command line (with or without leading `$TANDEM ` prefix).
/// Writes the `@TANDEM {...}\n` response into `out`. Returns bytes written, or 0 on overflow.
size_t process_command(const char* line,
                       const DeviceCapabilities& caps,
                       StickInputConfig& cfg,
                       const ConfigCallbacks& callbacks,
                       char* out,
                       size_t out_size) noexcept;

}  // namespace tandem::config
