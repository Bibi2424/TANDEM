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

inline constexpr uint8_t kTransportMaskUsbHid = 1u << 0;
inline constexpr uint8_t kTransportMaskBleHid = 1u << 1;
inline constexpr uint8_t kTransportMaskEspNowTx = 1u << 2;
inline constexpr uint8_t kTransportMaskBleConfig = 1u << 3;
inline constexpr uint8_t kTransportMaskEspNowRx = 1u << 4;
inline constexpr uint8_t kTransportMaskUsbSerial = 1u << 5;
inline constexpr uint8_t kTransportMaskWebHid = 1u << 6;

struct DeviceCapabilities {
  const char* board{"UNKNOWN"};
  StickId stick_id{StickId::Left};
  const char* transport_mode{"UNKNOWN"};
  bool synthetic{false};
  uint8_t axes_mask{0};
  uint32_t buttons_mask{0};
  bool has_hat{false};
  int32_t adc_max{kDefaultAxisMax};
  bool is_base_station{false};
  bool has_config{true};
  uint8_t transports_mask{0};
  const char* fw_version{nullptr};
  const char* git_commit{nullptr};
  const char* peer_status{nullptr};
};

struct TelemetrySnapshot {
  bool has_sample_hz{false};
  uint16_t sample_hz{0};
  bool has_tx_hz{false};
  uint16_t tx_hz{0};
  bool has_rssi{false};
  int8_t rssi_dbm{0};
  bool has_dropped{false};
  uint32_t dropped_packets{0};
  bool has_failsafe{false};
  bool failsafe_active{false};
  bool has_battery_mv{false};
  uint16_t battery_mv{0};
  bool has_battery_pct{false};
  uint8_t battery_pct{0};
  bool has_peer_status{false};
  const char* peer_status{nullptr};

  constexpr bool has_any() const noexcept {
    return has_sample_hz || has_tx_hz || has_rssi || has_dropped || has_failsafe || has_battery_mv || has_battery_pct ||
           (has_peer_status && peer_status != nullptr && peer_status[0] != '\0');
  }
};

struct ConfigCallbacks {
  bool (*on_save)(void* ctx){nullptr};
  bool (*on_reset)(void* ctx){nullptr};
  void (*on_live_changed)(void* ctx, bool enabled){nullptr};
  bool (*on_get_telemetry)(void* ctx, TelemetrySnapshot& out){nullptr};
  void* ctx{nullptr};
};

/// Formats a `caps` response line (`@TANDEM {...}\n`). Returns bytes written, or 0 on overflow.
size_t format_caps_response(const DeviceCapabilities& caps, char* out, size_t out_size) noexcept;

/// Formats a `status` response line (`@TANDEM {...}\n`). Returns bytes written, or 0 on overflow.
size_t format_status_response(const DeviceCapabilities& caps, const TelemetrySnapshot& tlm, char* out, size_t out_size) noexcept;

/// Formats a `get` response line (`@TANDEM {...}\n`). Returns bytes written, or 0 on overflow.
size_t format_get_response(const StickInputConfig& cfg, char* out, size_t out_size) noexcept;

/// Formats a `live` sample frame (`@TANDEM {...}\n`) without optional telemetry.
size_t format_live_response(const RawStickInput& raw, const StickState& state, bool synthetic, char* out, size_t out_size) noexcept;

/// Formats a `live` sample frame (`@TANDEM {...}\n`) with optional telemetry.
/// Returns bytes written, or 0 on overflow.
size_t format_live_response(const RawStickInput& raw, const StickState& state, bool synthetic, const TelemetrySnapshot& tlm, char* out,
                            size_t out_size) noexcept;

/// Parses and executes a config command line (with or without leading `$TANDEM ` prefix).
/// Writes the `@TANDEM {...}\n` response into `out`. Returns bytes written, or 0 on overflow.
size_t process_command(const char* line, const DeviceCapabilities& caps, StickInputConfig& cfg, const ConfigCallbacks& callbacks, char* out,
                       size_t out_size) noexcept;

}  // namespace tandem::config
