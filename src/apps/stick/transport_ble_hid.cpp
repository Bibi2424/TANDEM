#include "transport_ble_hid.h"

#if TANDEM_CAP_BLE

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <esp_mac.h>
#include <esp_timer.h>

#include <cstdio>
#include <cstring>

#include "tandem/device_name.h"
#include "tandem/hid_report.h"

namespace tandem::stick {

namespace {

constexpr const char* kDeviceNamePrefix = "TANDEM-BLE";
constexpr size_t kMaxDeviceNameBytes = 32;
constexpr const char* kManufacturer = "T.A.N.D.E.M.";
constexpr uint16_t kBleMtuBytes = 64;
constexpr int64_t kBleHidKeepaliveUs = 250000;  // 250 ms keepalive when idle

class ServerCallbacks final : public BLEServerCallbacks {
 public:
  void onConnect(BLEServer*) override { BleHidTransport::on_connect_cb(); }
  void onDisconnect(BLEServer*) override { BleHidTransport::on_disconnect_cb(); }
};

ServerCallbacks g_server_callbacks;
BLEHIDDevice* g_hid = nullptr;
BLECharacteristic* g_input_report = nullptr;
uint8_t g_report_map[tandem::kHidJoystickReportDescriptorBytes]{};

}  // namespace

std::atomic<bool> BleHidTransport::ble_connected_{false};
std::atomic<bool> BleHidTransport::ble_restart_adv_{false};

void BleHidTransport::on_connect_cb() noexcept {
  ble_connected_.store(true, std::memory_order_release);
}

void BleHidTransport::on_disconnect_cb() noexcept {
  // Release Bluedroid's static m_authCompleteSemaphore if the peer disconnected
  // before ESP_GAP_BLE_AUTH_CMPL_EVT fired, preventing BTC_TASK deadlock on reconnect.
  BLESecurity::signalAuthenticationComplete();
  BLESecurity::resetSecurity();
  ble_connected_.store(false, std::memory_order_release);
  ble_restart_adv_.store(true, std::memory_order_release);
}

bool BleHidTransport::init_ble_stack() noexcept {
  if (stack_initialized_) {
    return true;
  }

  uint8_t mac[6]{};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) {
    std::memset(mac, 0, sizeof(mac));
  }
  tandem::format_device_name(device_name_, sizeof(device_name_), kDeviceNamePrefix, mac);

  if (!BLEDevice::init(device_name_)) {
    return false;
  }
#if defined(CONFIG_NIMBLE_ENABLED)
  BLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
#endif
  BLEDevice::setMTU(kBleMtuBytes);

  // Work around Arduino-ESP32 BLESecurity retaining static state (including
  // Bluedroid's m_authCompleteSemaphore) across BLEDevice::deinit() and
  // disconnects where ESP_GAP_BLE_AUTH_CMPL_EVT never fired.
  BLESecurity::signalAuthenticationComplete();
  BLESecurity::resetSecurity();
  BLESecurity::setForceAuthentication(true);
  BLESecurity::setAuthenticationMode(true, false, true);
  BLESecurity::setCapability(ESP_IO_CAP_NONE);
  BLESecurity::setKeySize(16);
  BLESecurity::setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  BLEServer* server = BLEDevice::createServer();
  if (server == nullptr) {
    BLEDevice::deinit(false);
    return false;
  }
  server->setCallbacks(&g_server_callbacks);
  server->advertiseOnDisconnect(false);

  g_hid = new BLEHIDDevice(server);
  if (g_hid == nullptr) {
    BLEDevice::deinit(false);
    return false;
  }
  g_input_report = g_hid->inputReport(tandem::kHidReportIdJoystick);
  if (g_input_report == nullptr) {
    delete g_hid;
    g_hid = nullptr;
    BLEDevice::deinit(false);
    return false;
  }

  g_hid->manufacturer()->setValue(kManufacturer);
  g_hid->pnp(0x02, 0xE502, 0xA111, 0x0100);
  g_hid->hidInfo(0x00, 0x01);

  std::memcpy(g_report_map, tandem::kHidJoystickReportDescriptor, tandem::kHidJoystickReportDescriptorBytes);
  g_hid->reportMap(g_report_map, static_cast<uint16_t>(tandem::kHidJoystickReportDescriptorBytes));

  g_hid->startServices();
  g_hid->setBatteryLevel(100);

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  if (advertising == nullptr) {
    delete g_hid;
    g_hid = nullptr;
    g_input_report = nullptr;
    BLEDevice::deinit(false);
    return false;
  }
  advertising->setAppearance(HID_JOYSTICK);
  advertising->addServiceUUID(g_hid->hidService()->getUUID());
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);

  stack_initialized_ = true;
  return true;
}

bool BleHidTransport::start() noexcept {
  if (active_) {
    return true;
  }

  ble_connected_.store(false, std::memory_order_release);
  ble_restart_adv_.store(false, std::memory_order_release);
  has_last_report_ = false;
  last_notify_us_ = 0;

  if (!init_ble_stack()) {
    return false;
  }

  BLEDevice::startAdvertising();
  active_ = true;
  return true;
}

void BleHidTransport::stop() noexcept {
  if (!active_) {
    return;
  }
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  if (advertising != nullptr) {
    advertising->stop();
  }
  BLEServer* server = BLEDevice::getServer();
  if (server != nullptr && ble_connected_.load(std::memory_order_acquire)) {
    server->disconnect(server->getConnId());
  }
  if (stack_initialized_) {
    delete g_hid;
    g_hid = nullptr;
    g_input_report = nullptr;
    BLEDevice::deinit(false);
    BLESecurity::signalAuthenticationComplete();
    BLESecurity::resetSecurity();
    stack_initialized_ = false;
  }
  ble_connected_.store(false, std::memory_order_release);
  ble_restart_adv_.store(false, std::memory_order_release);
  has_last_report_ = false;
  last_notify_us_ = 0;
  active_ = false;
}

void BleHidTransport::poll() noexcept {
  if (!active_) {
    return;
  }
  if (!connected()) {
    has_last_report_ = false;
    last_notify_us_ = 0;
  }
  if (ble_restart_adv_.exchange(false, std::memory_order_acq_rel)) {
    BLEDevice::startAdvertising();
  }
}

bool BleHidTransport::connected() const noexcept {
  return active_ && ble_connected_.load(std::memory_order_acquire);
}

bool BleHidTransport::send(tandem::StickId, const tandem::StickState& state) noexcept {
  if (!connected() || g_input_report == nullptr) {
    return false;
  }
  const tandem::HidJoystickReport report = tandem::encode_hid_joystick_report(state);
  const int64_t now_us = esp_timer_get_time();
  const bool unchanged = has_last_report_ && (std::memcmp(report.bytes, last_report_.bytes, sizeof(report.bytes)) == 0);
  if (unchanged && (now_us - last_notify_us_) < kBleHidKeepaliveUs) {
    return false;
  }
  last_report_ = report;
  has_last_report_ = true;
  last_notify_us_ = now_us;
  g_input_report->setValue(report.bytes, sizeof(report.bytes));
  g_input_report->notify();
  ++sent_;
  return true;
}

void BleHidTransport::fill_telemetry(tandem::config::TelemetrySnapshot& out) const noexcept {
  out.has_peer_status = true;
  out.peer_status = connected() ? "BLE_CONN" : "BLE_ADV";
}

void BleHidTransport::format_stats(char* out, size_t out_len) const noexcept {
  std::snprintf(out, out_len, "conn=%d sent=%lu", connected() ? 1 : 0, static_cast<unsigned long>(sent_));
}

}  // namespace tandem::stick

#endif  // TANDEM_CAP_BLE
