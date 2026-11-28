// SPDX-License-Identifier: GPL-3.0-only
#include "tandem_hal/config_nvs.h"

#include <Preferences.h>

#include "tandem/config_storage.h"

namespace tandem::hal {
namespace {

constexpr char kNvsNamespace[] = "tandem";
constexpr char kNvsConfigKey[] = "stick_cfg";
constexpr char kNvsDeviceSettingsKey[] = "dev_cfg";

NvsLoadStatus read_nvs_blob(const char* key, uint8_t* dst, size_t expected_len) noexcept {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, true)) {
    return NvsLoadStatus::OpenFailed;
  }

  const size_t len = prefs.getBytesLength(key);
  if (len == 0) {
    prefs.end();
    return NvsLoadStatus::NotFound;
  }

  if (len != expected_len) {
    prefs.end();
    return NvsLoadStatus::CorruptOrInvalid;
  }

  const size_t read_len = prefs.getBytes(key, dst, expected_len);
  prefs.end();
  return (read_len == expected_len) ? NvsLoadStatus::Loaded : NvsLoadStatus::CorruptOrInvalid;
}

bool write_nvs_blob(const char* key, const uint8_t* data, size_t len) noexcept {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, false)) {
    return false;
  }

  const size_t written = prefs.putBytes(key, data, len);
  prefs.end();
  return written == len;
}

bool remove_nvs_key(const char* key) noexcept {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, false)) {
    return false;
  }

  if (prefs.isKey(key)) {
    prefs.remove(key);
  }
  const bool cleared = !prefs.isKey(key);
  prefs.end();
  return cleared;
}

}  // namespace

NvsLoadStatus load_stick_config(StickInputConfig& out) noexcept {
  tandem::PersistedConfigBlob blob{};
  const NvsLoadStatus status = read_nvs_blob(kNvsConfigKey, blob.bytes, sizeof(blob.bytes));
  if (status != NvsLoadStatus::Loaded) {
    return status;
  }

  const tandem::ConfigDecodeResult res = tandem::decode_stick_config(blob.bytes, sizeof(blob.bytes), out);
  return (res == tandem::ConfigDecodeResult::Ok) ? NvsLoadStatus::Loaded : NvsLoadStatus::CorruptOrInvalid;
}

bool save_stick_config(const StickInputConfig& cfg) noexcept {
  tandem::PersistedConfigBlob blob{};
  if (!tandem::encode_stick_config(cfg, blob)) {
    return false;
  }
  return write_nvs_blob(kNvsConfigKey, blob.bytes, sizeof(blob.bytes));
}

bool reset_stick_config() noexcept {
  return remove_nvs_key(kNvsConfigKey);
}

NvsLoadStatus load_device_settings(DeviceSettings& out) noexcept {
  tandem::PersistedDeviceSettingsBlob blob{};
  const NvsLoadStatus status = read_nvs_blob(kNvsDeviceSettingsKey, blob.bytes, sizeof(blob.bytes));
  if (status != NvsLoadStatus::Loaded) {
    return status;
  }

  const tandem::ConfigDecodeResult res = tandem::decode_device_settings(blob.bytes, sizeof(blob.bytes), out);
  return (res == tandem::ConfigDecodeResult::Ok) ? NvsLoadStatus::Loaded : NvsLoadStatus::CorruptOrInvalid;
}

bool save_device_settings(const DeviceSettings& settings) noexcept {
  tandem::PersistedDeviceSettingsBlob blob{};
  if (!tandem::encode_device_settings(settings, blob)) {
    return false;
  }
  return write_nvs_blob(kNvsDeviceSettingsKey, blob.bytes, sizeof(blob.bytes));
}

bool reset_device_settings() noexcept {
  return remove_nvs_key(kNvsDeviceSettingsKey);
}

const char* nvs_load_status_name(NvsLoadStatus status) noexcept {
  switch (status) {
    case NvsLoadStatus::Loaded:
      return "loaded";
    case NvsLoadStatus::NotFound:
      return "not_found_defaults";
    case NvsLoadStatus::OpenFailed:
      return "open_failed_defaults";
    case NvsLoadStatus::CorruptOrInvalid:
      return "corrupt_or_invalid_defaults";
    default:
      return "unknown";
  }
}

}  // namespace tandem::hal
