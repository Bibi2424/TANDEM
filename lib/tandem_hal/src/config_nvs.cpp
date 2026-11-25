#include "tandem_hal/config_nvs.h"

#include <Preferences.h>

#include "tandem/config_storage.h"

namespace tandem::hal {
namespace {

constexpr char kNvsNamespace[] = "tandem";
constexpr char kNvsConfigKey[] = "stick_cfg";

}  // namespace

NvsLoadStatus load_stick_config(StickInputConfig& out) noexcept {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, true)) {
    return NvsLoadStatus::OpenFailed;
  }

  const size_t len = prefs.getBytesLength(kNvsConfigKey);
  if (len == 0) {
    prefs.end();
    return NvsLoadStatus::NotFound;
  }

  if (len != tandem::kPersistedConfigBytes) {
    prefs.end();
    return NvsLoadStatus::CorruptOrInvalid;
  }

  tandem::PersistedConfigBlob blob{};
  const size_t read_len = prefs.getBytes(kNvsConfigKey, blob.bytes, sizeof(blob.bytes));
  prefs.end();

  if (read_len != sizeof(blob.bytes)) {
    return NvsLoadStatus::CorruptOrInvalid;
  }

  const tandem::ConfigDecodeResult res = tandem::decode_stick_config(blob.bytes, sizeof(blob.bytes), out);
  return (res == tandem::ConfigDecodeResult::Ok) ? NvsLoadStatus::Loaded : NvsLoadStatus::CorruptOrInvalid;
}

bool save_stick_config(const StickInputConfig& cfg) noexcept {
  tandem::PersistedConfigBlob blob{};
  if (!tandem::encode_stick_config(cfg, blob)) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, false)) {
    return false;
  }

  const size_t written = prefs.putBytes(kNvsConfigKey, blob.bytes, sizeof(blob.bytes));
  prefs.end();
  return written == sizeof(blob.bytes);
}

bool reset_stick_config() noexcept {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, false)) {
    return false;
  }

  if (prefs.isKey(kNvsConfigKey)) {
    prefs.remove(kNvsConfigKey);
  }
  const bool cleared = !prefs.isKey(kNvsConfigKey);
  prefs.end();
  return cleared;
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
