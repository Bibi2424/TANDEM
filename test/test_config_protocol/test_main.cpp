#include <unity.h>

#include <cstring>

#include "tandem/config_protocol.h"

using namespace tandem;
using namespace tandem::config;

void setUp() {}
void tearDown() {}

void test_caps_response_formats_all_fields() {
  DeviceCapabilities caps{};
  caps.board = "ESP32-S2";
  caps.stick_id = StickId::Right;
  caps.transport_mode = "USB_HID";
  caps.synthetic = true;
  caps.axes_mask = 0x0F;
  caps.buttons_mask = 0x0000003F;
  caps.has_hat = true;
  caps.adc_max = 4095;

  StickInputConfig cfg{};
  ConfigCallbacks cbs{};
  char out[kMaxResponseLength]{};

  const size_t n = process_command("$TANDEM caps\n", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "@TANDEM "));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd\":\"caps\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":true"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"board\":\"ESP32-S2\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"role\":\"R\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"mode\":\"USB_HID\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"synth\":true"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"axes_mask\":15"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"btns_mask\":63"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"hat\":true"));
}

void test_get_response_contains_all_axes() {
  DeviceCapabilities caps{};
  StickInputConfig cfg{};
  cfg.axes[0].enabled = true;
  cfg.axes[0].calibration = AxisCalibration{100, 2000, 3900};
  cfg.axes[0].deadzone = 0.08f;
  cfg.axes[0].invert = true;
  cfg.axes[0].curve = make_expo_curve(0.35f);

  ConfigCallbacks cbs{};
  char out[kMaxResponseLength]{};

  const size_t n = process_command("get", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd\":\"get\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"i\":0,\"en\":1,\"min\":100,\"ctr\":2000,\"max\":3900"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"inv\":1,\"crv\":1,\"p\":0.3500"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"i\":7,"));
}

void test_set_updates_axis_and_rejects_invalid_inputs() {
  DeviceCapabilities caps{};
  StickInputConfig cfg{};
  ConfigCallbacks cbs{};
  char out[kMaxResponseLength]{};

  size_t n = process_command("$TANDEM set 2 1 120 2050 3980 0.06 1 1 0.45\r\n", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd\":\"set\",\"ok\":true,\"i\":2"));
  TEST_ASSERT_TRUE(cfg.axes[2].enabled);
  TEST_ASSERT_EQUAL_INT32(120, cfg.axes[2].calibration.min);
  TEST_ASSERT_EQUAL_INT32(2050, cfg.axes[2].calibration.center);
  TEST_ASSERT_EQUAL_INT32(3980, cfg.axes[2].calibration.max);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.06f, cfg.axes[2].deadzone);
  TEST_ASSERT_TRUE(cfg.axes[2].invert);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CurveType::Expo), static_cast<uint8_t>(cfg.axes[2].curve.type));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.45f, cfg.axes[2].curve.param);

  // Degenerate bounds (min >= center) must be rejected and leave config unchanged.
  n = process_command("$TANDEM set 2 1 2050 2050 3980 0.06 0 0 0.0", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"invalid_bounds\""));
  TEST_ASSERT_EQUAL_INT32(120, cfg.axes[2].calibration.min);

  // Out-of-range axis index.
  n = process_command("$TANDEM set 8 1 0 2048 4095 0.05 0 0 0.0", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"invalid_axis\""));

  // Deadzone >= 1.0.
  n = process_command("$TANDEM set 0 1 0 2048 4095 1.0 0 0 0.0", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"invalid_deadzone\""));

  // Unknown curve type.
  n = process_command("$TANDEM set 0 1 0 2048 4095 0.05 0 2 0.0", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"invalid_curve\""));
}

void test_live_save_and_reset_callbacks() {
  struct HookState {
    bool live_enabled{false};
    int save_calls{0};
    int reset_calls{0};
  } hook;

  ConfigCallbacks cbs{};
  cbs.ctx = &hook;
  cbs.on_live_changed = [](void* ctx, bool enabled) { static_cast<HookState*>(ctx)->live_enabled = enabled; };
  cbs.on_reset = [](void* ctx, bool& reboot_required) -> bool {
    reboot_required = false;
    static_cast<HookState*>(ctx)->reset_calls++;
    return true;
  };

  DeviceCapabilities caps{};
  StickInputConfig cfg{};
  char out[kMaxResponseLength]{};

  process_command("$TANDEM live 1", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_TRUE(hook.live_enabled);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"streaming\":true"));

  process_command("$TANDEM live 0", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_FALSE(hook.live_enabled);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"streaming\":false"));

  // Without on_save callback, save reports nvs_not_implemented.
  process_command("$TANDEM save", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"nvs_not_implemented\""));

  // With on_save callback, save succeeds.
  cbs.on_save = [](void* ctx) -> bool {
    static_cast<HookState*>(ctx)->save_calls++;
    return true;
  };
  process_command("$TANDEM save", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_EQUAL_INT(1, hook.save_calls);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd\":\"save\",\"ok\":true"));

  process_command("$TANDEM reset", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_EQUAL_INT(1, hook.reset_calls);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd\":\"reset\",\"ok\":true"));
}

void test_live_sample_and_buffer_overflow_guard() {
  RawStickInput raw{};
  raw.axes[0] = 1000;
  raw.axes[1] = 3000;
  StickState state = make_centered_state();
  state.axes[0] = -16384;
  state.axes[1] = 16384;
  state.buttons = 0x05;
  state.hat = 3;

  char out[kMaxResponseLength]{};
  const size_t n = format_live_response(raw, state, false, out, sizeof(out));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd\":\"live\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"raw\":[1000,3000,"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"out\":[-16384,16384,"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"btns\":5,\"hat\":3"));
  TEST_ASSERT_NULL(std::strstr(out, "\"tlm\":"));

  char tiny[16]{};
  TEST_ASSERT_EQUAL_UINT32(0, format_live_response(raw, state, false, tiny, sizeof(tiny)));
}

void test_caps_extended_metadata_and_base_station_role() {
  DeviceCapabilities caps{};
  caps.board = "ESP32-S2";
  caps.is_base_station = true;
  caps.has_config = false;
  caps.transport_mode = "ESPNOW_RX_DUAL";
  caps.transports_mask = kTransportMaskEspNowRx | kTransportMaskUsbHid | kTransportMaskUsbSerial;
  caps.fw_version = "0.1.0";
  caps.git_commit = "abc1234";
  caps.peer_status = "L:CONN R:DISC";

  StickInputConfig cfg{};
  ConfigCallbacks cbs{};
  char out[kMaxResponseLength]{};

  const size_t n = process_command("$TANDEM caps", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"role\":\"BASE\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cfg\":false"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"fw\":\"0.1.0\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"git\":\"abc1234\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"transports\":[\"USB_HID\",\"ESPNOW_RX\",\"USB_SERIAL\"]"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"peer\":\"L:CONN R:DISC\""));
}

void test_status_command_formats_telemetry_and_rejects_args() {
  DeviceCapabilities caps{};
  caps.board = "ESP32-S3";
  caps.stick_id = StickId::Left;
  caps.transport_mode = "ESPNOW_TX";

  StickInputConfig cfg{};
  ConfigCallbacks cbs{};
  cbs.on_get_telemetry = [](void*, TelemetrySnapshot& tlm) -> bool {
    tlm.has_sample_hz = true;
    tlm.sample_hz = 1000;
    tlm.has_tx_hz = true;
    tlm.tx_hz = 250;
    tlm.has_rssi = true;
    tlm.rssi_dbm = -58;
    tlm.has_dropped = true;
    tlm.dropped_packets = 3;
    tlm.has_failsafe = true;
    tlm.failsafe_active = false;
    tlm.has_battery_mv = true;
    tlm.battery_mv = 4120;
    tlm.has_battery_pct = true;
    tlm.battery_pct = 92;
    tlm.has_peer_status = true;
    tlm.peer_status = "BROADCAST";
    return true;
  };

  char out[kMaxResponseLength]{};
  const size_t n = process_command("$TANDEM status", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd\":\"status\",\"ok\":true"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"hz\":1000"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"tx_hz\":250"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"rssi\":-58"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"drop\":3"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"fs\":false"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"bat_mv\":4120"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"bat_pct\":92"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"peer\":\"BROADCAST\""));

  const size_t err_n = process_command("$TANDEM status extra", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_GREATER_THAN(0, err_n);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"unexpected_args\""));
}

void test_live_response_with_partial_telemetry() {
  RawStickInput raw{};
  StickState state = make_centered_state();
  TelemetrySnapshot tlm{};
  tlm.has_tx_hz = true;
  tlm.tx_hz = 100;
  tlm.has_failsafe = true;
  tlm.failsafe_active = true;
  tlm.has_peer_status = true;
  tlm.peer_status = "BLE_CONN";

  char out[kMaxResponseLength]{};
  const size_t n = format_live_response(raw, state, true, tlm, out, sizeof(out));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"tlm\":{\"tx_hz\":100,\"fs\":true,\"peer\":\"BLE_CONN\"}"));
  TEST_ASSERT_NULL(std::strstr(out, "\"rssi\""));
  TEST_ASSERT_NULL(std::strstr(out, "\"bat_mv\""));
}

void test_caps_includes_webhid_transport() {
  DeviceCapabilities caps{};
  caps.board = "ESP32-S2";
  caps.transport_mode = "USB_HID";
  caps.transports_mask = kTransportMaskUsbHid | kTransportMaskUsbSerial | kTransportMaskWebHid;

  StickInputConfig cfg{};
  ConfigCallbacks cbs{};
  char out[kMaxResponseLength]{};

  const size_t n = process_command("$TANDEM caps", caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"transports\":[\"USB_HID\",\"USB_SERIAL\",\"WEBHID\"]"));
}

void test_stick_mode_and_role_commands() {
  struct HookState {
    StickTransportMode last_mode{StickTransportMode::UsbHid};
    StickId last_id{StickId::Left};
    int mode_calls{0};
    int role_calls{0};
  } hook;

  DeviceCapabilities caps{};
  caps.board = "ESP32-S2";
  caps.stick_id = StickId::Left;
  caps.is_base_station = false;
  caps.transports_mask = kTransportMaskUsbHid | kTransportMaskEspNowTx | kTransportMaskUsbSerial | kTransportMaskWebHid;

  StickInputConfig cfg{};
  ConfigCallbacks cbs{};
  cbs.ctx = &hook;
  cbs.on_set_transport_mode = [](void* ctx, StickTransportMode mode) -> bool {
    auto* st = static_cast<HookState*>(ctx);
    st->last_mode = mode;
    st->mode_calls++;
    return true;
  };
  cbs.on_set_stick_id = [](void* ctx, StickId id) -> bool {
    auto* st = static_cast<HookState*>(ctx);
    st->last_id = id;
    st->role_calls++;
    return true;
  };

  char out[kMaxResponseLength]{};

  // Supported stick mode (case-insensitive) succeeds.
  TEST_ASSERT_GREATER_THAN(0, process_command("$TANDEM mode espnow_tx", caps, cfg, cbs, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd\":\"mode\",\"ok\":true,\"mode\":\"ESPNOW_TX\",\"reboot\":false"));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StickTransportMode::EspNowTx), static_cast<uint8_t>(hook.last_mode));
  TEST_ASSERT_EQUAL_INT(1, hook.mode_calls);

  // Unsupported mode for S2 fails without invoking callback.
  TEST_ASSERT_GREATER_THAN(0, process_command("$TANDEM mode BLE_HID", caps, cfg, cbs, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"unsupported_mode\""));
  TEST_ASSERT_EQUAL_INT(1, hook.mode_calls);

  // Unknown mode name fails.
  TEST_ASSERT_GREATER_THAN(0, process_command("$TANDEM mode BOGUS", caps, cfg, cbs, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"invalid_mode\""));

  // Missing or extra args fail.
  TEST_ASSERT_GREATER_THAN(0, process_command("$TANDEM mode", caps, cfg, cbs, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"invalid_args\""));
  TEST_ASSERT_GREATER_THAN(0, process_command("$TANDEM mode USB_HID extra", caps, cfg, cbs, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"invalid_args\""));

  // Role updates (short & long tokens) succeed.
  TEST_ASSERT_GREATER_THAN(0, process_command("$TANDEM role r", caps, cfg, cbs, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd\":\"role\",\"ok\":true,\"role\":\"R\""));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StickId::Right), static_cast<uint8_t>(hook.last_id));

  TEST_ASSERT_GREATER_THAN(0, process_command("$TANDEM role left", caps, cfg, cbs, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd\":\"role\",\"ok\":true,\"role\":\"L\""));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StickId::Left), static_cast<uint8_t>(hook.last_id));
  TEST_ASSERT_EQUAL_INT(2, hook.role_calls);

  // Invalid role fails.
  TEST_ASSERT_GREATER_THAN(0, process_command("$TANDEM role CENTER", caps, cfg, cbs, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"invalid_role\""));
}

void test_base_station_mode_and_reset_reboot_flag() {
  struct HookState {
    BaseOutputMode mode{BaseOutputMode::Dual};
  } hook;

  DeviceCapabilities caps{};
  caps.board = "ESP32-S2";
  caps.is_base_station = true;
  caps.has_config = false;

  StickInputConfig cfg{};
  ConfigCallbacks cbs{};
  cbs.ctx = &hook;
  cbs.on_set_base_mode = [](void* ctx, BaseOutputMode mode, bool& reboot_required) -> bool {
    auto* st = static_cast<HookState*>(ctx);
    reboot_required = (st->mode != mode);
    st->mode = mode;
    return true;
  };
  cbs.on_reset = [](void* ctx, bool& reboot_required) -> bool {
    auto* st = static_cast<HookState*>(ctx);
    reboot_required = (st->mode != BaseOutputMode::Dual);
    st->mode = BaseOutputMode::Dual;
    return true;
  };

  char out[kMaxResponseLength]{};

  // Switch Dual -> Combined triggers reboot:true.
  TEST_ASSERT_GREATER_THAN(0, process_command("$TANDEM mode combined", caps, cfg, cbs, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd\":\"mode\",\"ok\":true,\"mode\":\"ESPNOW_RX_COMBINED\",\"reboot\":true"));

  // Selecting active mode again reports reboot:false.
  TEST_ASSERT_GREATER_THAN(0, process_command("$TANDEM mode ESPNOW_RX_COMBINED", caps, cfg, cbs, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd\":\"mode\",\"ok\":true,\"mode\":\"ESPNOW_RX_COMBINED\",\"reboot\":false"));

  // Stick transport mode rejected on Base Station.
  TEST_ASSERT_GREATER_THAN(0, process_command("$TANDEM mode USB_HID", caps, cfg, cbs, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"invalid_mode\""));

  // Role command unsupported on Base Station.
  TEST_ASSERT_GREATER_THAN(0, process_command("$TANDEM role L", caps, cfg, cbs, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"unsupported\""));

  // Reset from Combined back to Dual emits reboot:true.
  TEST_ASSERT_GREATER_THAN(0, process_command("$TANDEM reset", caps, cfg, cbs, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd\":\"reset\",\"ok\":true,\"reboot\":true"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_caps_response_formats_all_fields);
  RUN_TEST(test_get_response_contains_all_axes);
  RUN_TEST(test_set_updates_axis_and_rejects_invalid_inputs);
  RUN_TEST(test_live_save_and_reset_callbacks);
  RUN_TEST(test_live_sample_and_buffer_overflow_guard);
  RUN_TEST(test_caps_extended_metadata_and_base_station_role);
  RUN_TEST(test_status_command_formats_telemetry_and_rejects_args);
  RUN_TEST(test_live_response_with_partial_telemetry);
  RUN_TEST(test_caps_includes_webhid_transport);
  RUN_TEST(test_stick_mode_and_role_commands);
  RUN_TEST(test_base_station_mode_and_reset_reboot_flag);
  return UNITY_END();
}
