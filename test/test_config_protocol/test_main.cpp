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

  size_t n = process_command("$TANDEM set 2 1 120 2050 3980 0.06 1 1 0.45\r\n",
                             caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd\":\"set\",\"ok\":true,\"i\":2"));
  TEST_ASSERT_TRUE(cfg.axes[2].enabled);
  TEST_ASSERT_EQUAL_INT32(120, cfg.axes[2].calibration.min);
  TEST_ASSERT_EQUAL_INT32(2050, cfg.axes[2].calibration.center);
  TEST_ASSERT_EQUAL_INT32(3980, cfg.axes[2].calibration.max);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.06f, cfg.axes[2].deadzone);
  TEST_ASSERT_TRUE(cfg.axes[2].invert);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CurveType::Expo),
                          static_cast<uint8_t>(cfg.axes[2].curve.type));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.45f, cfg.axes[2].curve.param);

  // Degenerate bounds (min >= center) must be rejected and leave config unchanged.
  n = process_command("$TANDEM set 2 1 2050 2050 3980 0.06 0 0 0.0",
                      caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"invalid_bounds\""));
  TEST_ASSERT_EQUAL_INT32(120, cfg.axes[2].calibration.min);

  // Out-of-range axis index.
  n = process_command("$TANDEM set 8 1 0 2048 4095 0.05 0 0 0.0",
                      caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"invalid_axis\""));

  // Deadzone >= 1.0.
  n = process_command("$TANDEM set 0 1 0 2048 4095 1.0 0 0 0.0",
                      caps, cfg, cbs, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ok\":false,\"err\":\"invalid_deadzone\""));

  // Unknown curve type.
  n = process_command("$TANDEM set 0 1 0 2048 4095 0.05 0 2 0.0",
                      caps, cfg, cbs, out, sizeof(out));
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
  cbs.on_live_changed = [](void* ctx, bool enabled) {
    static_cast<HookState*>(ctx)->live_enabled = enabled;
  };
  cbs.on_reset = [](void* ctx) -> bool {
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

  char tiny[16]{};
  TEST_ASSERT_EQUAL_UINT32(0, format_live_response(raw, state, false, tiny, sizeof(tiny)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_caps_response_formats_all_fields);
  RUN_TEST(test_get_response_contains_all_axes);
  RUN_TEST(test_set_updates_axis_and_rejects_invalid_inputs);
  RUN_TEST(test_live_save_and_reset_callbacks);
  RUN_TEST(test_live_sample_and_buffer_overflow_guard);
  return UNITY_END();
}
