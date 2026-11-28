// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "tandem/config_storage.h"

namespace {

tandem::StickInputConfig make_sample_config() {
  tandem::StickInputConfig cfg{};
  cfg.axes[0].enabled = true;
  cfg.axes[0].calibration = {120, 2035, 3980};
  cfg.axes[0].deadzone = 0.08f;
  cfg.axes[0].invert = true;
  cfg.axes[0].curve = tandem::make_expo_curve(0.35f);

  cfg.axes[1].enabled = true;
  cfg.axes[1].calibration = {50, 2100, 4050};
  cfg.axes[1].deadzone = 0.02f;
  cfg.axes[1].invert = false;
  cfg.axes[1].curve = tandem::make_linear_curve();

  cfg.axes[2].enabled = false;
  cfg.axes[2].calibration = {0, 2048, 4095};
  return cfg;
}

void write_u32_le(uint8_t* dst, uint32_t val) {
  dst[0] = static_cast<uint8_t>(val & 0xFFU);
  dst[1] = static_cast<uint8_t>((val >> 8U) & 0xFFU);
  dst[2] = static_cast<uint8_t>((val >> 16U) & 0xFFU);
  dst[3] = static_cast<uint8_t>((val >> 24U) & 0xFFU);
}

void test_blob_size_and_header() {
  const tandem::StickInputConfig cfg = make_sample_config();
  tandem::PersistedConfigBlob blob{};
  TEST_ASSERT_TRUE(tandem::encode_stick_config(cfg, blob));

  TEST_ASSERT_EQUAL_UINT(204U, sizeof(blob.bytes));
  TEST_ASSERT_EQUAL_UINT8('T', blob.bytes[0]);
  TEST_ASSERT_EQUAL_UINT8('M', blob.bytes[1]);
  TEST_ASSERT_EQUAL_UINT8('F', blob.bytes[2]);
  TEST_ASSERT_EQUAL_UINT8('C', blob.bytes[3]);
  TEST_ASSERT_EQUAL_UINT8(1U, blob.bytes[4]);
  TEST_ASSERT_EQUAL_UINT8(0U, blob.bytes[5]);
  TEST_ASSERT_EQUAL_UINT8(tandem::kMaxAxes, blob.bytes[6]);
}

void test_roundtrip_encode_decode() {
  const tandem::StickInputConfig original = make_sample_config();
  tandem::PersistedConfigBlob blob{};
  TEST_ASSERT_TRUE(tandem::encode_stick_config(original, blob));

  tandem::StickInputConfig decoded{};
  const tandem::ConfigDecodeResult res = tandem::decode_stick_config(blob.bytes, sizeof(blob.bytes), decoded);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::Ok), static_cast<uint8_t>(res));

  for (uint8_t i = 0; i < tandem::kMaxAxes; ++i) {
    TEST_ASSERT_EQUAL(original.axes[i].enabled, decoded.axes[i].enabled);
    TEST_ASSERT_EQUAL(original.axes[i].invert, decoded.axes[i].invert);
    TEST_ASSERT_EQUAL_INT32(original.axes[i].calibration.min, decoded.axes[i].calibration.min);
    TEST_ASSERT_EQUAL_INT32(original.axes[i].calibration.center, decoded.axes[i].calibration.center);
    TEST_ASSERT_EQUAL_INT32(original.axes[i].calibration.max, decoded.axes[i].calibration.max);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, original.axes[i].deadzone, decoded.axes[i].deadzone);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(original.axes[i].curve.type), static_cast<uint8_t>(decoded.axes[i].curve.type));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, original.axes[i].curve.param, decoded.axes[i].curve.param);
  }
}

void test_corrupt_crc_rejected_and_preserves_out() {
  const tandem::StickInputConfig cfg = make_sample_config();
  tandem::PersistedConfigBlob blob{};
  TEST_ASSERT_TRUE(tandem::encode_stick_config(cfg, blob));

  tandem::StickInputConfig sentinel{};
  sentinel.axes[0].calibration = {10, 500, 1000};

  // Flip a bit in payload
  tandem::PersistedConfigBlob bad_payload = blob;
  bad_payload.bytes[20] ^= 0x01U;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::BadCrc),
                          static_cast<uint8_t>(tandem::decode_stick_config(bad_payload.bytes, sizeof(bad_payload.bytes), sentinel)));
  TEST_ASSERT_EQUAL_INT32(500, sentinel.axes[0].calibration.center);

  // Flip a bit in CRC trailer
  tandem::PersistedConfigBlob bad_crc = blob;
  bad_crc.bytes[tandem::kPersistedConfigBytes - 1U] ^= 0x80U;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::BadCrc),
                          static_cast<uint8_t>(tandem::decode_stick_config(bad_crc.bytes, sizeof(bad_crc.bytes), sentinel)));
  TEST_ASSERT_EQUAL_INT32(500, sentinel.axes[0].calibration.center);
}

void test_header_and_length_errors() {
  const tandem::StickInputConfig cfg = make_sample_config();
  tandem::PersistedConfigBlob blob{};
  TEST_ASSERT_TRUE(tandem::encode_stick_config(cfg, blob));

  tandem::StickInputConfig out{};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::InvalidLength),
                          static_cast<uint8_t>(tandem::decode_stick_config(nullptr, sizeof(blob.bytes), out)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::InvalidLength),
                          static_cast<uint8_t>(tandem::decode_stick_config(blob.bytes, sizeof(blob.bytes) - 1U, out)));

  tandem::PersistedConfigBlob bad_magic = blob;
  bad_magic.bytes[0] = 'X';
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::BadMagic),
                          static_cast<uint8_t>(tandem::decode_stick_config(bad_magic.bytes, sizeof(bad_magic.bytes), out)));

  tandem::PersistedConfigBlob bad_ver = blob;
  bad_ver.bytes[4] = 99U;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::UnsupportedVersion),
                          static_cast<uint8_t>(tandem::decode_stick_config(bad_ver.bytes, sizeof(bad_ver.bytes), out)));

  tandem::PersistedConfigBlob bad_axes = blob;
  bad_axes.bytes[6] = 4U;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::InvalidAxisCount),
                          static_cast<uint8_t>(tandem::decode_stick_config(bad_axes.bytes, sizeof(bad_axes.bytes), out)));
}

void test_invalid_config_rejected() {
  tandem::StickInputConfig bad_bounds = make_sample_config();
  bad_bounds.axes[0].calibration = {2000, 2000, 4000};
  tandem::PersistedConfigBlob blob{};
  TEST_ASSERT_FALSE(tandem::encode_stick_config(bad_bounds, blob));

  tandem::StickInputConfig bad_dz = make_sample_config();
  bad_dz.axes[1].deadzone = 1.0f;
  TEST_ASSERT_FALSE(tandem::encode_stick_config(bad_dz, blob));

  tandem::StickInputConfig bad_nan = make_sample_config();
  bad_nan.axes[0].curve.param = NAN;
  TEST_ASSERT_FALSE(tandem::encode_stick_config(bad_nan, blob));

  tandem::StickInputConfig bad_curve = make_sample_config();
  bad_curve.axes[0].curve.type = static_cast<tandem::CurveType>(42);
  TEST_ASSERT_FALSE(tandem::encode_stick_config(bad_curve, blob));

  // Hand-craft a valid-CRC blob with an invalid curve type to test decode validation
  const tandem::StickInputConfig good = make_sample_config();
  TEST_ASSERT_TRUE(tandem::encode_stick_config(good, blob));
  blob.bytes[tandem::kConfigHeaderBytes + 16U] = 42U;
  const uint32_t new_crc = tandem::crc32_ieee(blob.bytes, tandem::kPersistedConfigBytes - 4U);
  write_u32_le(&blob.bytes[tandem::kPersistedConfigBytes - 4U], new_crc);

  tandem::StickInputConfig out{};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::InvalidConfig),
                          static_cast<uint8_t>(tandem::decode_stick_config(blob.bytes, sizeof(blob.bytes), out)));
}

void test_device_settings_blob_size_and_header() {
  tandem::DeviceSettings settings{};
  settings.transport_mode = tandem::StickTransportMode::EspNowTx;
  settings.stick_id = tandem::StickId::Right;
  settings.base_mode = tandem::BaseOutputMode::Combined;

  tandem::PersistedDeviceSettingsBlob blob{};
  TEST_ASSERT_TRUE(tandem::encode_device_settings(settings, blob));

  TEST_ASSERT_EQUAL_UINT(16U, sizeof(blob.bytes));
  TEST_ASSERT_EQUAL_UINT8('T', blob.bytes[0]);
  TEST_ASSERT_EQUAL_UINT8('M', blob.bytes[1]);
  TEST_ASSERT_EQUAL_UINT8('D', blob.bytes[2]);
  TEST_ASSERT_EQUAL_UINT8('S', blob.bytes[3]);
  TEST_ASSERT_EQUAL_UINT8(1U, blob.bytes[4]);
  TEST_ASSERT_EQUAL_UINT8(0U, blob.bytes[5]);
}

void test_device_settings_roundtrip() {
  const tandem::DeviceSettings cases[] = {
      {tandem::StickTransportMode::UsbHid, tandem::StickId::Left, tandem::BaseOutputMode::Dual},
      {tandem::StickTransportMode::BleHid, tandem::StickId::Right, tandem::BaseOutputMode::Dual},
      {tandem::StickTransportMode::EspNowTx, tandem::StickId::Left, tandem::BaseOutputMode::Combined},
      {tandem::StickTransportMode::BleConfig, tandem::StickId::Right, tandem::BaseOutputMode::Combined},
  };

  for (const auto& original : cases) {
    tandem::PersistedDeviceSettingsBlob blob{};
    TEST_ASSERT_TRUE(tandem::encode_device_settings(original, blob));

    tandem::DeviceSettings decoded{};
    const tandem::ConfigDecodeResult res = tandem::decode_device_settings(blob.bytes, sizeof(blob.bytes), decoded);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::Ok), static_cast<uint8_t>(res));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(original.transport_mode), static_cast<uint8_t>(decoded.transport_mode));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(original.stick_id), static_cast<uint8_t>(decoded.stick_id));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(original.base_mode), static_cast<uint8_t>(decoded.base_mode));
  }
}

void test_device_settings_corrupt_crc_preserves_out() {
  const tandem::DeviceSettings original{tandem::StickTransportMode::EspNowTx, tandem::StickId::Right, tandem::BaseOutputMode::Combined};
  tandem::PersistedDeviceSettingsBlob blob{};
  TEST_ASSERT_TRUE(tandem::encode_device_settings(original, blob));

  tandem::DeviceSettings sentinel{tandem::StickTransportMode::BleHid, tandem::StickId::Left, tandem::BaseOutputMode::Dual};

  tandem::PersistedDeviceSettingsBlob bad_payload = blob;
  bad_payload.bytes[8] ^= 0x01U;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::BadCrc),
                          static_cast<uint8_t>(tandem::decode_device_settings(bad_payload.bytes, sizeof(bad_payload.bytes), sentinel)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::StickTransportMode::BleHid), static_cast<uint8_t>(sentinel.transport_mode));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::StickId::Left), static_cast<uint8_t>(sentinel.stick_id));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::BaseOutputMode::Dual), static_cast<uint8_t>(sentinel.base_mode));

  tandem::PersistedDeviceSettingsBlob bad_crc = blob;
  bad_crc.bytes[tandem::kPersistedDeviceSettingsBytes - 1U] ^= 0x80U;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::BadCrc),
                          static_cast<uint8_t>(tandem::decode_device_settings(bad_crc.bytes, sizeof(bad_crc.bytes), sentinel)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::StickTransportMode::BleHid), static_cast<uint8_t>(sentinel.transport_mode));
}

void test_device_settings_header_and_length_errors() {
  const tandem::DeviceSettings original{tandem::StickTransportMode::UsbHid, tandem::StickId::Left, tandem::BaseOutputMode::Dual};
  tandem::PersistedDeviceSettingsBlob blob{};
  TEST_ASSERT_TRUE(tandem::encode_device_settings(original, blob));

  tandem::DeviceSettings out{};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::InvalidLength),
                          static_cast<uint8_t>(tandem::decode_device_settings(nullptr, sizeof(blob.bytes), out)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::InvalidLength),
                          static_cast<uint8_t>(tandem::decode_device_settings(blob.bytes, sizeof(blob.bytes) - 1U, out)));

  tandem::PersistedDeviceSettingsBlob bad_magic = blob;
  bad_magic.bytes[0] = 'X';
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::BadMagic),
                          static_cast<uint8_t>(tandem::decode_device_settings(bad_magic.bytes, sizeof(bad_magic.bytes), out)));

  tandem::PersistedDeviceSettingsBlob bad_ver = blob;
  bad_ver.bytes[4] = 99U;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::UnsupportedVersion),
                          static_cast<uint8_t>(tandem::decode_device_settings(bad_ver.bytes, sizeof(bad_ver.bytes), out)));
}

void test_device_settings_invalid_values_rejected() {
  tandem::PersistedDeviceSettingsBlob blob{};

  tandem::DeviceSettings bad_mode{static_cast<tandem::StickTransportMode>(9), tandem::StickId::Left, tandem::BaseOutputMode::Dual};
  TEST_ASSERT_FALSE(tandem::encode_device_settings(bad_mode, blob));

  tandem::DeviceSettings bad_id{tandem::StickTransportMode::UsbHid, tandem::StickId::Unknown, tandem::BaseOutputMode::Dual};
  TEST_ASSERT_FALSE(tandem::encode_device_settings(bad_id, blob));

  tandem::DeviceSettings bad_base{tandem::StickTransportMode::UsbHid, tandem::StickId::Left, static_cast<tandem::BaseOutputMode>(3)};
  TEST_ASSERT_FALSE(tandem::encode_device_settings(bad_base, blob));

  // Hand-craft valid-CRC blobs with invalid enum values to verify decode validation.
  const tandem::DeviceSettings good{tandem::StickTransportMode::UsbHid, tandem::StickId::Left, tandem::BaseOutputMode::Dual};
  TEST_ASSERT_TRUE(tandem::encode_device_settings(good, blob));
  blob.bytes[9] = static_cast<uint8_t>(tandem::StickId::Unknown);
  const uint32_t crc = tandem::crc32_ieee(blob.bytes, tandem::kPersistedDeviceSettingsBytes - 4U);
  write_u32_le(&blob.bytes[tandem::kPersistedDeviceSettingsBytes - 4U], crc);

  tandem::DeviceSettings out{};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::ConfigDecodeResult::InvalidConfig),
                          static_cast<uint8_t>(tandem::decode_device_settings(blob.bytes, sizeof(blob.bytes), out)));
}

void test_device_settings_capability_clamping() {
  const tandem::BoardTransportCaps s2_caps{true, false, true};
  const tandem::DeviceSettings s2_defaults{tandem::StickTransportMode::UsbHid, tandem::StickId::Left, tandem::BaseOutputMode::Dual};
  const tandem::DeviceSettings s2_loaded{tandem::StickTransportMode::BleHid, tandem::StickId::Right, tandem::BaseOutputMode::Combined};
  const tandem::DeviceSettings s2_clamped = tandem::clamp_device_settings(s2_loaded, s2_caps, s2_defaults);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::StickTransportMode::UsbHid), static_cast<uint8_t>(s2_clamped.transport_mode));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::StickId::Right), static_cast<uint8_t>(s2_clamped.stick_id));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::BaseOutputMode::Combined), static_cast<uint8_t>(s2_clamped.base_mode));

  const tandem::BoardTransportCaps wroom_caps{false, true, true};
  const tandem::DeviceSettings wroom_defaults{tandem::StickTransportMode::BleHid, tandem::StickId::Right, tandem::BaseOutputMode::Dual};
  const tandem::DeviceSettings wroom_loaded{tandem::StickTransportMode::UsbHid, tandem::StickId::Unknown, static_cast<tandem::BaseOutputMode>(7)};
  const tandem::DeviceSettings wroom_clamped = tandem::clamp_device_settings(wroom_loaded, wroom_caps, wroom_defaults);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::StickTransportMode::BleHid), static_cast<uint8_t>(wroom_clamped.transport_mode));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::StickId::Right), static_cast<uint8_t>(wroom_clamped.stick_id));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(tandem::BaseOutputMode::Dual), static_cast<uint8_t>(wroom_clamped.base_mode));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_blob_size_and_header);
  RUN_TEST(test_roundtrip_encode_decode);
  RUN_TEST(test_corrupt_crc_rejected_and_preserves_out);
  RUN_TEST(test_header_and_length_errors);
  RUN_TEST(test_invalid_config_rejected);
  RUN_TEST(test_device_settings_blob_size_and_header);
  RUN_TEST(test_device_settings_roundtrip);
  RUN_TEST(test_device_settings_corrupt_crc_preserves_out);
  RUN_TEST(test_device_settings_header_and_length_errors);
  RUN_TEST(test_device_settings_invalid_values_rejected);
  RUN_TEST(test_device_settings_capability_clamping);
  return UNITY_END();
}
