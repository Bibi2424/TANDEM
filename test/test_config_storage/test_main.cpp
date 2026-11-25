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
  return UNITY_END();
}
