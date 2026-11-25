#include "tandem/config_protocol.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "tandem/core_info.h"

namespace tandem::config {

namespace {

bool append_fmt(char* out, size_t out_size, size_t& pos, const char* fmt, ...) noexcept {
  if (out == nullptr || pos >= out_size) {
    return false;
  }
  va_list args;
  va_start(args, fmt);
  const int written = std::vsnprintf(out + pos, out_size - pos, fmt, args);
  va_end(args);
  if (written < 0 || static_cast<size_t>(written) >= (out_size - pos)) {
    return false;
  }
  pos += static_cast<size_t>(written);
  return true;
}

const char* skip_ws(const char* p) noexcept {
  while (p != nullptr && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
    ++p;
  }
  return p;
}

bool parse_int32(const char*& p, int32_t& out) noexcept {
  p = skip_ws(p);
  if (p == nullptr || *p == '\0') {
    return false;
  }
  char* end = nullptr;
  const long val = std::strtol(p, &end, 10);
  if (end == p) {
    return false;
  }
  if (*end != '\0' && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') {
    return false;
  }
  out = static_cast<int32_t>(val);
  p = end;
  return true;
}

bool parse_float(const char*& p, float& out) noexcept {
  p = skip_ws(p);
  if (p == nullptr || *p == '\0') {
    return false;
  }
  char* end = nullptr;
  const float val = std::strtof(p, &end);
  if (end == p) {
    return false;
  }
  if (*end != '\0' && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') {
    return false;
  }
  out = val;
  p = end;
  return true;
}

bool has_no_trailing_tokens(const char* p) noexcept {
  p = skip_ws(p);
  return p != nullptr && *p == '\0';
}

const char* stick_role_str(StickId id) noexcept {
  switch (id) {
    case StickId::Left:
      return "L";
    case StickId::Right:
      return "R";
    case StickId::Unknown:
    default:
      return "U";
  }
}

const char* caps_role_str(const DeviceCapabilities& caps) noexcept {
  if (caps.is_base_station) {
    return "BASE";
  }
  return stick_role_str(caps.stick_id);
}

bool append_transports_array(char* out, size_t out_size, size_t& pos, uint8_t mask) noexcept {
  struct TransportEntry {
    uint8_t bit;
    const char* name;
  };
  static constexpr TransportEntry kEntries[] = {
      {kTransportMaskUsbHid, "USB_HID"},    {kTransportMaskBleHid, "BLE_HID"},     {kTransportMaskEspNowTx, "ESPNOW_TX"},
      {kTransportMaskBleConfig, "BLE_CFG"}, {kTransportMaskEspNowRx, "ESPNOW_RX"}, {kTransportMaskUsbSerial, "USB_SERIAL"},
      {kTransportMaskWebHid, "WEBHID"},
  };
  if (!append_fmt(out, out_size, pos, "[")) {
    return false;
  }
  bool first = true;
  for (const auto& e : kEntries) {
    if ((mask & e.bit) != 0U) {
      if (!append_fmt(out, out_size, pos, "%s\"%s\"", first ? "" : ",", e.name)) {
        return false;
      }
      first = false;
    }
  }
  return append_fmt(out, out_size, pos, "]");
}

bool append_tlm_fields(char* out, size_t out_size, size_t& pos, const TelemetrySnapshot& tlm) noexcept {
  if (!append_fmt(out, out_size, pos, "{")) {
    return false;
  }
  bool first = true;
  auto sep = [&first]() -> const char* {
    const char* s = first ? "" : ",";
    first = false;
    return s;
  };

  if (tlm.has_sample_hz && !append_fmt(out, out_size, pos, "%s\"hz\":%u", sep(), static_cast<unsigned>(tlm.sample_hz))) {
    return false;
  }
  if (tlm.has_tx_hz && !append_fmt(out, out_size, pos, "%s\"tx_hz\":%u", sep(), static_cast<unsigned>(tlm.tx_hz))) {
    return false;
  }
  if (tlm.has_rssi && !append_fmt(out, out_size, pos, "%s\"rssi\":%d", sep(), static_cast<int>(tlm.rssi_dbm))) {
    return false;
  }
  if (tlm.has_dropped && !append_fmt(out, out_size, pos, "%s\"drop\":%lu", sep(), static_cast<unsigned long>(tlm.dropped_packets))) {
    return false;
  }
  if (tlm.has_failsafe && !append_fmt(out, out_size, pos, "%s\"fs\":%s", sep(), tlm.failsafe_active ? "true" : "false")) {
    return false;
  }
  if (tlm.has_battery_mv && !append_fmt(out, out_size, pos, "%s\"bat_mv\":%u", sep(), static_cast<unsigned>(tlm.battery_mv))) {
    return false;
  }
  if (tlm.has_battery_pct && !append_fmt(out, out_size, pos, "%s\"bat_pct\":%u", sep(), static_cast<unsigned>(tlm.battery_pct))) {
    return false;
  }
  if (tlm.has_peer_status && tlm.peer_status != nullptr && tlm.peer_status[0] != '\0' &&
      !append_fmt(out, out_size, pos, "%s\"peer\":\"%s\"", sep(), tlm.peer_status)) {
    return false;
  }
  return append_fmt(out, out_size, pos, "}");
}

size_t format_error(const char* cmd, const char* err, char* out, size_t out_size) noexcept {
  size_t pos = 0;
  if (!append_fmt(out, out_size, pos, "%s{\"v\":%u,\"cmd\":\"%s\",\"ok\":false,\"err\":\"%s\"}\n", kResponsePrefix,
                  static_cast<unsigned>(kProtocolVersion), cmd != nullptr ? cmd : "unknown", err != nullptr ? err : "error")) {
    return 0;
  }
  return pos;
}

bool token_equals(const char* tok, size_t tok_len, const char* literal) noexcept {
  return std::strlen(literal) == tok_len && std::strncmp(tok, literal, tok_len) == 0;
}

}  // namespace

size_t format_caps_response(const DeviceCapabilities& caps, char* out, size_t out_size) noexcept {
  size_t pos = 0;
  const char* fw = (caps.fw_version != nullptr && caps.fw_version[0] != '\0') ? caps.fw_version : core_version();
  const char* git = (caps.git_commit != nullptr && caps.git_commit[0] != '\0') ? caps.git_commit : core_git_commit();
  if (!append_fmt(out, out_size, pos,
                  "%s{\"v\":%u,\"cmd\":\"caps\",\"ok\":true,\"board\":\"%s\",\"role\":\"%s\","
                  "\"mode\":\"%s\",\"synth\":%s,\"max_axes\":%u,\"axes_mask\":%u,"
                  "\"btns_mask\":%lu,\"hat\":%s,\"adc_max\":%ld,\"cfg\":%s,"
                  "\"fw\":\"%s\",\"git\":\"%s\",\"transports\":",
                  kResponsePrefix, static_cast<unsigned>(kProtocolVersion), caps.board != nullptr ? caps.board : "UNKNOWN", caps_role_str(caps),
                  caps.transport_mode != nullptr ? caps.transport_mode : "UNKNOWN", caps.synthetic ? "true" : "false",
                  static_cast<unsigned>(kMaxAxes), static_cast<unsigned>(caps.axes_mask), static_cast<unsigned long>(caps.buttons_mask),
                  caps.has_hat ? "true" : "false", static_cast<long>(caps.adc_max), caps.has_config ? "true" : "false", fw, git)) {
    return 0;
  }
  if (!append_transports_array(out, out_size, pos, caps.transports_mask)) {
    return 0;
  }
  if (caps.peer_status != nullptr && caps.peer_status[0] != '\0') {
    if (!append_fmt(out, out_size, pos, ",\"peer\":\"%s\"", caps.peer_status)) {
      return 0;
    }
  }
  if (!append_fmt(out, out_size, pos, "}\n")) {
    return 0;
  }
  return pos;
}

size_t format_status_response(const DeviceCapabilities& caps, const TelemetrySnapshot& tlm, char* out, size_t out_size) noexcept {
  size_t pos = 0;
  if (!append_fmt(out, out_size, pos,
                  "%s{\"v\":%u,\"cmd\":\"status\",\"ok\":true,\"board\":\"%s\",\"role\":\"%s\","
                  "\"mode\":\"%s\",\"synth\":%s,\"tlm\":",
                  kResponsePrefix, static_cast<unsigned>(kProtocolVersion), caps.board != nullptr ? caps.board : "UNKNOWN", caps_role_str(caps),
                  caps.transport_mode != nullptr ? caps.transport_mode : "UNKNOWN", caps.synthetic ? "true" : "false")) {
    return 0;
  }
  if (!append_tlm_fields(out, out_size, pos, tlm)) {
    return 0;
  }
  if (!append_fmt(out, out_size, pos, "}\n")) {
    return 0;
  }
  return pos;
}

size_t format_get_response(const StickInputConfig& cfg, char* out, size_t out_size) noexcept {
  size_t pos = 0;
  if (!append_fmt(out, out_size, pos, "%s{\"v\":%u,\"cmd\":\"get\",\"ok\":true,\"axes\":[", kResponsePrefix,
                  static_cast<unsigned>(kProtocolVersion))) {
    return 0;
  }

  for (uint8_t i = 0; i < kMaxAxes; ++i) {
    const AxisConfig& ax = cfg.axes[i];
    if (!append_fmt(out, out_size, pos,
                    "%s{\"i\":%u,\"en\":%u,\"min\":%ld,\"ctr\":%ld,\"max\":%ld,"
                    "\"dz\":%.4f,\"inv\":%u,\"crv\":%u,\"p\":%.4f}",
                    (i == 0) ? "" : ",", static_cast<unsigned>(i), ax.enabled ? 1u : 0u, static_cast<long>(ax.calibration.min),
                    static_cast<long>(ax.calibration.center), static_cast<long>(ax.calibration.max), static_cast<double>(ax.deadzone),
                    ax.invert ? 1u : 0u, static_cast<unsigned>(ax.curve.type), static_cast<double>(ax.curve.param))) {
      return 0;
    }
  }

  if (!append_fmt(out, out_size, pos, "]}\n")) {
    return 0;
  }
  return pos;
}

size_t format_live_response(const RawStickInput& raw, const StickState& state, bool synthetic, char* out, size_t out_size) noexcept {
  return format_live_response(raw, state, synthetic, TelemetrySnapshot{}, out, out_size);
}

size_t format_live_response(const RawStickInput& raw, const StickState& state, bool synthetic, const TelemetrySnapshot& tlm, char* out,
                            size_t out_size) noexcept {
  size_t pos = 0;
  if (!append_fmt(out, out_size, pos, "%s{\"v\":%u,\"cmd\":\"live\",\"ok\":true,\"synth\":%s,\"raw\":[", kResponsePrefix,
                  static_cast<unsigned>(kProtocolVersion), synthetic ? "true" : "false")) {
    return 0;
  }

  for (uint8_t i = 0; i < kMaxAxes; ++i) {
    if (!append_fmt(out, out_size, pos, "%s%ld", (i == 0) ? "" : ",", static_cast<long>(raw.axes[i]))) {
      return 0;
    }
  }

  if (!append_fmt(out, out_size, pos, "],\"out\":[")) {
    return 0;
  }

  for (uint8_t i = 0; i < kMaxAxes; ++i) {
    if (!append_fmt(out, out_size, pos, "%s%d", (i == 0) ? "" : ",", static_cast<int>(state.axes[i]))) {
      return 0;
    }
  }

  if (!append_fmt(out, out_size, pos, "],\"btns\":%lu,\"hat\":%u", static_cast<unsigned long>(state.buttons), static_cast<unsigned>(state.hat))) {
    return 0;
  }
  if (tlm.has_any()) {
    if (!append_fmt(out, out_size, pos, ",\"tlm\":") || !append_tlm_fields(out, out_size, pos, tlm)) {
      return 0;
    }
  }
  if (!append_fmt(out, out_size, pos, "}\n")) {
    return 0;
  }
  return pos;
}

size_t process_command(const char* line, const DeviceCapabilities& caps, StickInputConfig& cfg, const ConfigCallbacks& callbacks, char* out,
                       size_t out_size) noexcept {
  if (out == nullptr || out_size == 0) {
    return 0;
  }
  if (line == nullptr) {
    return format_error("unknown", "empty_command", out, out_size);
  }

  const char* p = skip_ws(line);
  const size_t prefix_len = std::strlen(kCommandPrefix);
  if (std::strncmp(p, kCommandPrefix, prefix_len) == 0) {
    p = skip_ws(p + prefix_len);
  } else if (std::strcmp(p, "$TANDEM") == 0) {
    return format_error("unknown", "missing_command", out, out_size);
  }

  if (*p == '\0') {
    return format_error("unknown", "empty_command", out, out_size);
  }

  const char* cmd_start = p;
  while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
    ++p;
  }
  const size_t cmd_len = static_cast<size_t>(p - cmd_start);

  if (token_equals(cmd_start, cmd_len, "caps")) {
    if (!has_no_trailing_tokens(p)) {
      return format_error("caps", "unexpected_args", out, out_size);
    }
    return format_caps_response(caps, out, out_size);
  }

  if (token_equals(cmd_start, cmd_len, "status")) {
    if (!has_no_trailing_tokens(p)) {
      return format_error("status", "unexpected_args", out, out_size);
    }
    TelemetrySnapshot tlm{};
    if (callbacks.on_get_telemetry != nullptr) {
      callbacks.on_get_telemetry(callbacks.ctx, tlm);
    }
    return format_status_response(caps, tlm, out, out_size);
  }

  if (token_equals(cmd_start, cmd_len, "get")) {
    if (!has_no_trailing_tokens(p)) {
      return format_error("get", "unexpected_args", out, out_size);
    }
    return format_get_response(cfg, out, out_size);
  }

  if (token_equals(cmd_start, cmd_len, "set")) {
    int32_t axis = 0;
    int32_t en = 0;
    int32_t min_v = 0;
    int32_t ctr_v = 0;
    int32_t max_v = 0;
    float dz = 0.0f;
    int32_t inv = 0;
    int32_t crv = 0;
    float param = 0.0f;

    if (!parse_int32(p, axis) || !parse_int32(p, en) || !parse_int32(p, min_v) || !parse_int32(p, ctr_v) || !parse_int32(p, max_v) ||
        !parse_float(p, dz) || !parse_int32(p, inv) || !parse_int32(p, crv) || !parse_float(p, param) || !has_no_trailing_tokens(p)) {
      return format_error("set", "invalid_args", out, out_size);
    }

    if (axis < 0 || axis >= static_cast<int32_t>(kMaxAxes)) {
      return format_error("set", "invalid_axis", out, out_size);
    }
    if ((en != 0 && en != 1) || (inv != 0 && inv != 1)) {
      return format_error("set", "invalid_bool", out, out_size);
    }
    if (!(min_v < ctr_v && ctr_v < max_v)) {
      return format_error("set", "invalid_bounds", out, out_size);
    }
    if (!std::isfinite(dz) || dz < 0.0f || dz >= 1.0f) {
      return format_error("set", "invalid_deadzone", out, out_size);
    }
    if (crv != static_cast<int32_t>(CurveType::Linear) && crv != static_cast<int32_t>(CurveType::Expo)) {
      return format_error("set", "invalid_curve", out, out_size);
    }
    if (!std::isfinite(param) || param < 0.0f || param > 1.0f) {
      return format_error("set", "invalid_curve_param", out, out_size);
    }

    AxisConfig& dst = cfg.axes[axis];
    dst.enabled = (en == 1);
    dst.calibration.min = min_v;
    dst.calibration.center = ctr_v;
    dst.calibration.max = max_v;
    dst.deadzone = dz;
    dst.invert = (inv == 1);
    dst.curve.type = static_cast<CurveType>(crv);
    dst.curve.param = param;

    size_t pos = 0;
    if (!append_fmt(out, out_size, pos, "%s{\"v\":%u,\"cmd\":\"set\",\"ok\":true,\"i\":%ld}\n", kResponsePrefix,
                    static_cast<unsigned>(kProtocolVersion), static_cast<long>(axis))) {
      return 0;
    }
    return pos;
  }

  if (token_equals(cmd_start, cmd_len, "live")) {
    int32_t enabled = 0;
    if (!parse_int32(p, enabled) || !has_no_trailing_tokens(p) || (enabled != 0 && enabled != 1)) {
      return format_error("live", "invalid_args", out, out_size);
    }
    if (callbacks.on_live_changed != nullptr) {
      callbacks.on_live_changed(callbacks.ctx, enabled == 1);
    }
    size_t pos = 0;
    if (!append_fmt(out, out_size, pos, "%s{\"v\":%u,\"cmd\":\"live\",\"ok\":true,\"streaming\":%s}\n", kResponsePrefix,
                    static_cast<unsigned>(kProtocolVersion), (enabled == 1) ? "true" : "false")) {
      return 0;
    }
    return pos;
  }

  if (token_equals(cmd_start, cmd_len, "save")) {
    if (!has_no_trailing_tokens(p)) {
      return format_error("save", "unexpected_args", out, out_size);
    }
    if (callbacks.on_save == nullptr) {
      return format_error("save", "nvs_not_implemented", out, out_size);
    }
    if (!callbacks.on_save(callbacks.ctx)) {
      return format_error("save", "save_failed", out, out_size);
    }
    size_t pos = 0;
    if (!append_fmt(out, out_size, pos, "%s{\"v\":%u,\"cmd\":\"save\",\"ok\":true}\n", kResponsePrefix, static_cast<unsigned>(kProtocolVersion))) {
      return 0;
    }
    return pos;
  }

  if (token_equals(cmd_start, cmd_len, "reset")) {
    if (!has_no_trailing_tokens(p)) {
      return format_error("reset", "unexpected_args", out, out_size);
    }
    if (callbacks.on_reset != nullptr) {
      if (!callbacks.on_reset(callbacks.ctx)) {
        return format_error("reset", "reset_failed", out, out_size);
      }
    } else {
      cfg = StickInputConfig{};
    }
    size_t pos = 0;
    if (!append_fmt(out, out_size, pos, "%s{\"v\":%u,\"cmd\":\"reset\",\"ok\":true}\n", kResponsePrefix, static_cast<unsigned>(kProtocolVersion))) {
      return 0;
    }
    return pos;
  }

  return format_error("unknown", "unknown_command", out, out_size);
}

}  // namespace tandem::config
