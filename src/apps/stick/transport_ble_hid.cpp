#include "transport_ble_hid.h"

#if TANDEM_CAP_BLE

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>

#include <cstdio>
#include <cstring>

#include "tandem/hid_report.h"

namespace tandem::stick {

namespace {

constexpr const char* kDeviceName = "TANDEM-BLE-Stick";
constexpr const char* kManufacturer = "T.A.N.D.E.M.";
constexpr uint16_t kBleMtuBytes = 64;

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
  ble_connected_.store(false, std::memory_order_release);
  ble_restart_adv_.store(true, std::memory_order_release);
}

bool BleHidTransport::start() noexcept {
  if (active_) {
    return true;
  }

  ble_connected_.store(false, std::memory_order_release);
  ble_restart_adv_.store(false, std::memory_order_release);

  if (!BLEDevice::init(kDeviceName)) {
    return false;
  }
  BLEDevice::setMTU(kBleMtuBytes);

  BLESecurity::setAuthenticationMode(true, false, true);
  BLESecurity::setCapability(ESP_IO_CAP_NONE);
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

  std::memcpy(g_report_map,
              tandem::kHidJoystickReportDescriptor,
              tandem::kHidJoystickReportDescriptorBytes);
  g_hid->reportMap(g_report_map,
                   static_cast<uint16_t>(tandem::kHidJoystickReportDescriptorBytes));

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
  ble_connected_.store(false, std::memory_order_release);
  ble_restart_adv_.store(false, std::memory_order_release);
  g_input_report = nullptr;
  delete g_hid;
  g_hid = nullptr;
  // Pass false so BT memory is preserved if the user switches back to BLE later.
  BLEDevice::deinit(false);
  active_ = false;
}

void BleHidTransport::poll() noexcept {
  if (!active_) {
    return;
  }
  if (ble_restart_adv_.exchange(false, std::memory_order_acq_rel)) {
    BLEDevice::startAdvertising();
  }
}

bool BleHidTransport::connected() const noexcept {
  return active_ && ble_connected_.load(std::memory_order_acquire);
}

bool BleHidTransport::send(tandem::StickId,
                           const tandem::StickState& state) noexcept {
  if (!connected() || g_input_report == nullptr) {
    return false;
  }
  const tandem::HidJoystickReport report = tandem::encode_hid_joystick_report(state);
  g_input_report->setValue(report.bytes, sizeof(report.bytes));
  g_input_report->notify();
  ++sent_;
  return true;
}

void BleHidTransport::format_stats(char* out, size_t out_len) const noexcept {
  std::snprintf(out,
                out_len,
                "conn=%d sent=%lu",
                connected() ? 1 : 0,
                static_cast<unsigned long>(sent_));
}

}  // namespace tandem::stick

#endif  // TANDEM_CAP_BLE
