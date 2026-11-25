#include "tandem/config_storage.h"

#include <cmath>
#include <cstring>

namespace tandem {
namespace {

constexpr uint8_t kFlagEnabled = 0x01U;
constexpr uint8_t kFlagInvert = 0x02U;
constexpr size_t kCrcPayloadBytes = kPersistedConfigBytes - kConfigTrailerBytes;

void write_u16_le(uint8_t* dst, uint16_t val) noexcept {
  dst[0] = static_cast<uint8_t>(val & 0xFFU);
  dst[1] = static_cast<uint8_t>((val >> 8U) & 0xFFU);
}

void write_u32_le(uint8_t* dst, uint32_t val) noexcept {
  dst[0] = static_cast<uint8_t>(val & 0xFFU);
  dst[1] = static_cast<uint8_t>((val >> 8U) & 0xFFU);
  dst[2] = static_cast<uint8_t>((val >> 16U) & 0xFFU);
  dst[3] = static_cast<uint8_t>((val >> 24U) & 0xFFU);
}

void write_i32_le(uint8_t* dst, int32_t val) noexcept {
  write_u32_le(dst, static_cast<uint32_t>(val));
}

void write_f32_le(uint8_t* dst, float val) noexcept {
  static_assert(sizeof(float) == sizeof(uint32_t), "IEEE-754 32-bit float required");
  uint32_t bits = 0;
  std::memcpy(&bits, &val, sizeof(bits));
  write_u32_le(dst, bits);
}

uint16_t read_u16_le(const uint8_t* src) noexcept {
  return static_cast<uint16_t>(static_cast<uint16_t>(src[0]) | (static_cast<uint16_t>(src[1]) << 8U));
}

uint32_t read_u32_le(const uint8_t* src) noexcept {
  return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8U) | (static_cast<uint32_t>(src[2]) << 16U) |
         (static_cast<uint32_t>(src[3]) << 24U);
}

int32_t read_i32_le(const uint8_t* src) noexcept {
  return static_cast<int32_t>(read_u32_le(src));
}

float read_f32_le(const uint8_t* src) noexcept {
  static_assert(sizeof(float) == sizeof(uint32_t), "IEEE-754 32-bit float required");
  const uint32_t bits = read_u32_le(src);
  float val = 0.0f;
  std::memcpy(&val, &bits, sizeof(val));
  return val;
}

}  // namespace

uint32_t crc32_ieee(const uint8_t* data, size_t len) noexcept {
  uint32_t crc = 0xFFFFFFFFU;
  if (data == nullptr) {
    return 0U;
  }
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint32_t>(data[i]);
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      if ((crc & 1U) != 0U) {
        crc = (crc >> 1U) ^ 0xEDB88320U;
      } else {
        crc >>= 1U;
      }
    }
  }
  return crc ^ 0xFFFFFFFFU;
}

bool is_valid_axis_config(const AxisConfig& axis) noexcept {
  if (!axis.calibration.is_valid()) {
    return false;
  }
  if (!std::isfinite(axis.deadzone) || axis.deadzone < 0.0f || axis.deadzone >= 1.0f) {
    return false;
  }
  if (axis.curve.type != CurveType::Linear && axis.curve.type != CurveType::Expo) {
    return false;
  }
  if (!std::isfinite(axis.curve.param) || axis.curve.param < 0.0f || axis.curve.param > 1.0f) {
    return false;
  }
  return true;
}

bool is_valid_stick_config(const StickInputConfig& cfg) noexcept {
  for (uint8_t i = 0; i < kMaxAxes; ++i) {
    if (!is_valid_axis_config(cfg.axes[i])) {
      return false;
    }
  }
  return true;
}

bool encode_stick_config(const StickInputConfig& cfg, PersistedConfigBlob& out) noexcept {
  if (!is_valid_stick_config(cfg)) {
    return false;
  }

  std::memset(out.bytes, 0, sizeof(out.bytes));
  out.bytes[0] = kConfigStorageMagic0;
  out.bytes[1] = kConfigStorageMagic1;
  out.bytes[2] = kConfigStorageMagic2;
  out.bytes[3] = kConfigStorageMagic3;
  write_u16_le(&out.bytes[4], kConfigStorageVersion);
  out.bytes[6] = kMaxAxes;
  out.bytes[7] = 0U;

  for (uint8_t i = 0; i < kMaxAxes; ++i) {
    const AxisConfig& axis = cfg.axes[i];
    uint8_t* rec = &out.bytes[kConfigHeaderBytes + (i * kConfigAxisRecordBytes)];
    write_i32_le(&rec[0], axis.calibration.min);
    write_i32_le(&rec[4], axis.calibration.center);
    write_i32_le(&rec[8], axis.calibration.max);
    write_f32_le(&rec[12], axis.deadzone);
    rec[16] = static_cast<uint8_t>(axis.curve.type);
    rec[17] = static_cast<uint8_t>((axis.enabled ? kFlagEnabled : 0U) | (axis.invert ? kFlagInvert : 0U));
    write_u16_le(&rec[18], 0U);
    write_f32_le(&rec[20], axis.curve.param);
  }

  const uint32_t crc = crc32_ieee(out.bytes, kCrcPayloadBytes);
  write_u32_le(&out.bytes[kCrcPayloadBytes], crc);
  return true;
}

ConfigDecodeResult decode_stick_config(const uint8_t* data, size_t len, StickInputConfig& out) noexcept {
  if (data == nullptr || len != kPersistedConfigBytes) {
    return ConfigDecodeResult::InvalidLength;
  }

  if (data[0] != kConfigStorageMagic0 || data[1] != kConfigStorageMagic1 || data[2] != kConfigStorageMagic2 || data[3] != kConfigStorageMagic3) {
    return ConfigDecodeResult::BadMagic;
  }

  const uint16_t version = read_u16_le(&data[4]);
  if (version != kConfigStorageVersion) {
    return ConfigDecodeResult::UnsupportedVersion;
  }

  if (data[6] != kMaxAxes) {
    return ConfigDecodeResult::InvalidAxisCount;
  }

  const uint32_t expected_crc = crc32_ieee(data, kCrcPayloadBytes);
  const uint32_t stored_crc = read_u32_le(&data[kCrcPayloadBytes]);
  if (stored_crc != expected_crc) {
    return ConfigDecodeResult::BadCrc;
  }

  StickInputConfig decoded{};
  for (uint8_t i = 0; i < kMaxAxes; ++i) {
    const uint8_t* rec = &data[kConfigHeaderBytes + (i * kConfigAxisRecordBytes)];
    AxisConfig& axis = decoded.axes[i];
    axis.calibration.min = read_i32_le(&rec[0]);
    axis.calibration.center = read_i32_le(&rec[4]);
    axis.calibration.max = read_i32_le(&rec[8]);
    axis.deadzone = read_f32_le(&rec[12]);
    axis.curve.type = static_cast<CurveType>(rec[16]);
    axis.enabled = (rec[17] & kFlagEnabled) != 0U;
    axis.invert = (rec[17] & kFlagInvert) != 0U;
    axis.curve.param = read_f32_le(&rec[20]);
  }

  if (!is_valid_stick_config(decoded)) {
    return ConfigDecodeResult::InvalidConfig;
  }

  out = decoded;
  return ConfigDecodeResult::Ok;
}

const char* config_decode_result_name(ConfigDecodeResult result) noexcept {
  switch (result) {
    case ConfigDecodeResult::Ok:
      return "ok";
    case ConfigDecodeResult::InvalidLength:
      return "invalid_length";
    case ConfigDecodeResult::BadMagic:
      return "bad_magic";
    case ConfigDecodeResult::UnsupportedVersion:
      return "unsupported_version";
    case ConfigDecodeResult::InvalidAxisCount:
      return "invalid_axis_count";
    case ConfigDecodeResult::BadCrc:
      return "bad_crc";
    case ConfigDecodeResult::InvalidConfig:
      return "invalid_config";
    default:
      return "unknown";
  }
}

}  // namespace tandem
