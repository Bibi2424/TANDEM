#include "transport_ble_config.h"

#if TANDEM_CAP_BLE

#if defined(CONFIG_BLUEDROID_ENABLED)
#include <BLE2902.h>
#endif
#include <BLEDevice.h>
#include <BLEServer.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include <cstdio>
#include <cstring>

#include "tandem/ble_config_gatt.h"
#include "tandem/device_name.h"

namespace tandem::stick {

namespace {

constexpr const char* kConfigDeviceNamePrefix = "TANDEM-CFG";
constexpr size_t kMaxDeviceNameBytes = 32;
constexpr uint16_t kBleConfigMtuBytes = 247;
constexpr size_t kCommandQueueDepth = 4;

struct QueuedCommand {
  char line[tandem::config::kMaxCommandLength]{};
  bool overflow{false};
};

portMUX_TYPE g_rx_mux = portMUX_INITIALIZER_UNLOCKED;
tandem::config::BleLineAssembler g_assembler;
QueuedCommand g_cmd_queue[kCommandQueueDepth]{};
size_t g_cmd_head = 0;
size_t g_cmd_tail = 0;
size_t g_cmd_count = 0;

void enqueue_command_locked(const char* line, bool overflow) noexcept {
  if (g_cmd_count >= kCommandQueueDepth) {
    return;
  }
  QueuedCommand& slot = g_cmd_queue[g_cmd_tail];
  slot.overflow = overflow;
  if (!overflow && line != nullptr) {
    std::strncpy(slot.line, line, sizeof(slot.line) - 1);
    slot.line[sizeof(slot.line) - 1] = '\0';
  } else {
    slot.line[0] = '\0';
  }
  g_cmd_tail = (g_cmd_tail + 1) % kCommandQueueDepth;
  ++g_cmd_count;
}

class ConfigServerCallbacks final : public BLEServerCallbacks {
 public:
  void onConnect(BLEServer*) override { BleConfigTransport::on_connect_cb(); }
  void onDisconnect(BLEServer*) override {
    BleConfigTransport::on_disconnect_cb();
  }
};

class CommandCharCallbacks final : public BLECharacteristicCallbacks {
 public:
  void onWrite(BLECharacteristic* characteristic) override {
    if (characteristic == nullptr) {
      return;
    }
    const String val = characteristic->getValue();
    if (val.length() > 0) {
      BleConfigTransport::on_cmd_write_cb(
          reinterpret_cast<const uint8_t*>(val.c_str()), val.length());
    }
  }
};

ConfigServerCallbacks g_server_callbacks;
CommandCharCallbacks g_cmd_callbacks;
BLECharacteristic* g_resp_char = nullptr;
BLECharacteristic* g_live_char = nullptr;

bool notify_chunked(BLECharacteristic* characteristic,
                    const char* data,
                    size_t len) noexcept {
  if (characteristic == nullptr || data == nullptr || len == 0) {
    return false;
  }
  BLEServer* server = BLEDevice::getServer();
  uint16_t mtu = 23;
  if (server != nullptr) {
    const uint16_t peer_mtu = server->getPeerMTU(server->getConnId());
    if (peer_mtu > 3) {
      mtu = peer_mtu;
    }
  }
  const size_t chunk_bytes = tandem::config::clamp_ble_chunk_bytes(mtu);
  tandem::config::for_each_ble_chunk(
      data, len, chunk_bytes, [characteristic](const uint8_t* ptr, size_t slice) {
        characteristic->setValue(const_cast<uint8_t*>(ptr), slice);
        characteristic->notify();
      });
  return true;
}

}  // namespace

std::atomic<bool> BleConfigTransport::ble_connected_{false};
std::atomic<bool> BleConfigTransport::ble_restart_adv_{false};

void BleConfigTransport::on_connect_cb() noexcept {
  portENTER_CRITICAL(&g_rx_mux);
  g_assembler.reset();
  g_cmd_head = 0;
  g_cmd_tail = 0;
  g_cmd_count = 0;
  portEXIT_CRITICAL(&g_rx_mux);
  ble_connected_.store(true, std::memory_order_release);
}

void BleConfigTransport::on_disconnect_cb() noexcept {
  ble_connected_.store(false, std::memory_order_release);
  ble_restart_adv_.store(true, std::memory_order_release);
}

void BleConfigTransport::on_cmd_write_cb(const uint8_t* data,
                                         size_t len) noexcept {
  if (data == nullptr || len == 0) {
    return;
  }
  char line[tandem::config::kMaxCommandLength]{};
  portENTER_CRITICAL(&g_rx_mux);
  for (size_t i = 0; i < len; ++i) {
    const auto res = g_assembler.push_byte(data[i], line, sizeof(line));
    if (res == tandem::config::BleLineAssembler::FeedResult::LineReady) {
      enqueue_command_locked(line, false);
    } else if (res == tandem::config::BleLineAssembler::FeedResult::Overflow) {
      enqueue_command_locked(nullptr, true);
    }
  }
  portEXIT_CRITICAL(&g_rx_mux);
}

bool BleConfigTransport::init_ble_stack() noexcept {
  if (stack_initialized_) {
    return true;
  }

  uint8_t mac[6]{};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) {
    std::memset(mac, 0, sizeof(mac));
  }
  tandem::format_device_name(
      device_name_, sizeof(device_name_), kConfigDeviceNamePrefix, mac);

  if (!BLEDevice::init(device_name_)) {
    return false;
  }
#if defined(CONFIG_NIMBLE_ENABLED)
  // Use a deterministic Random Static Address distinct from the public MAC used
  // by BLE_HID so Windows does not mix GATT caches or bond state between modes.
  uint8_t cfg_addr[6] = {
      static_cast<uint8_t>(mac[5] ^ 0x01U),
      mac[4],
      mac[3],
      mac[2],
      mac[1],
      static_cast<uint8_t>((mac[0] ^ 0x02U) | 0xC0U),
  };
  BLEDevice::setOwnAddr(cfg_addr);
  BLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
#endif
  BLEDevice::setMTU(kBleConfigMtuBytes);

  BLEServer* server = BLEDevice::createServer();
  if (server == nullptr) {
    BLEDevice::deinit(false);
    return false;
  }
  server->setCallbacks(&g_server_callbacks);
  server->advertiseOnDisconnect(false);

  BLEService* service =
      server->createService(tandem::config::kBleConfigServiceUuid);
  if (service == nullptr) {
    BLEDevice::deinit(false);
    return false;
  }

  BLECharacteristic* cmd_char = service->createCharacteristic(
      tandem::config::kBleConfigCommandCharUuid,
      BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_WRITE_NR);
  g_resp_char = service->createCharacteristic(
      tandem::config::kBleConfigResponseCharUuid,
      BLECharacteristic::PROPERTY_NOTIFY);
  g_live_char = service->createCharacteristic(
      tandem::config::kBleConfigLiveCharUuid,
      BLECharacteristic::PROPERTY_NOTIFY);

  if (cmd_char == nullptr || g_resp_char == nullptr || g_live_char == nullptr) {
    g_resp_char = nullptr;
    g_live_char = nullptr;
    BLEDevice::deinit(false);
    return false;
  }

  cmd_char->setCallbacks(&g_cmd_callbacks);
#if defined(CONFIG_BLUEDROID_ENABLED)
  g_resp_char->addDescriptor(new BLE2902());
  g_live_char->addDescriptor(new BLE2902());
#endif

  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  if (advertising == nullptr) {
    g_resp_char = nullptr;
    g_live_char = nullptr;
    BLEDevice::deinit(false);
    return false;
  }
  advertising->addServiceUUID(tandem::config::kBleConfigServiceUuid);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
#if defined(CONFIG_BLUEDROID_ENABLED)
  // Use a deterministic Random Static Address distinct from the public MAC used
  // by BLE_HID so Windows does not mix GATT caches or bond state between modes.
  esp_bd_addr_t cfg_addr = {
      static_cast<uint8_t>((mac[0] ^ 0x02U) | 0xC0U),
      mac[1],
      mac[2],
      mac[3],
      mac[4],
      static_cast<uint8_t>(mac[5] ^ 0x01U),
  };
  advertising->setDeviceAddress(cfg_addr, BLE_ADDR_TYPE_RANDOM);
#endif

  stack_initialized_ = true;
  return true;
}

bool BleConfigTransport::start() noexcept {
  if (active_) {
    return true;
  }

  ble_connected_.store(false, std::memory_order_release);
  ble_restart_adv_.store(false, std::memory_order_release);
  portENTER_CRITICAL(&g_rx_mux);
  g_assembler.reset();
  g_cmd_head = 0;
  g_cmd_tail = 0;
  g_cmd_count = 0;
  portEXIT_CRITICAL(&g_rx_mux);

  if (!init_ble_stack()) {
    return false;
  }

  BLEDevice::startAdvertising();
  active_ = true;
  return true;
}

void BleConfigTransport::stop() noexcept {
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
    g_resp_char = nullptr;
    g_live_char = nullptr;
    BLEDevice::deinit(false);
    stack_initialized_ = false;
  }
  ble_connected_.store(false, std::memory_order_release);
  ble_restart_adv_.store(false, std::memory_order_release);
  active_ = false;
}

void BleConfigTransport::poll() noexcept {
  if (!active_) {
    return;
  }
  if (ble_restart_adv_.exchange(false, std::memory_order_acq_rel)) {
    BLEDevice::startAdvertising();
  }
}

bool BleConfigTransport::connected() const noexcept {
  return active_ && ble_connected_.load(std::memory_order_acquire);
}

bool BleConfigTransport::send(tandem::StickId,
                              const tandem::StickState&) noexcept {
  // Config mode does not emit HID/ESP-NOW input reports; telemetry is sent via send_live().
  return false;
}

bool BleConfigTransport::pop_command(char* out,
                                     size_t out_size,
                                     bool& overflow) noexcept {
  if (out == nullptr || out_size == 0) {
    return false;
  }
  bool has_item = false;
  portENTER_CRITICAL(&g_rx_mux);
  if (g_cmd_count > 0) {
    const QueuedCommand& slot = g_cmd_queue[g_cmd_head];
    overflow = slot.overflow;
    if (!overflow) {
      std::strncpy(out, slot.line, out_size - 1);
      out[out_size - 1] = '\0';
    } else {
      out[0] = '\0';
    }
    g_cmd_head = (g_cmd_head + 1) % kCommandQueueDepth;
    --g_cmd_count;
    has_item = true;
  }
  portEXIT_CRITICAL(&g_rx_mux);
  if (has_item) {
    ++rx_cmds_;
  }
  return has_item;
}

bool BleConfigTransport::send_response(const char* data, size_t len) noexcept {
  if (!connected()) {
    return false;
  }
  if (notify_chunked(g_resp_char, data, len)) {
    ++tx_frames_;
    return true;
  }
  return false;
}

bool BleConfigTransport::send_live(const char* data, size_t len) noexcept {
  if (!connected()) {
    return false;
  }
  if (notify_chunked(g_live_char, data, len)) {
    ++tx_frames_;
    return true;
  }
  return false;
}

void BleConfigTransport::fill_telemetry(tandem::config::TelemetrySnapshot& out) const noexcept {
  out.has_peer_status = true;
  out.peer_status = connected() ? "BLE_CFG_CONN" : "BLE_CFG_ADV";
}

void BleConfigTransport::format_stats(char* out, size_t out_len) const noexcept {
  std::snprintf(out,
                out_len,
                "conn=%d rx=%lu tx=%lu",
                connected() ? 1 : 0,
                static_cast<unsigned long>(rx_cmds_),
                static_cast<unsigned long>(tx_frames_));
}

}  // namespace tandem::stick

#endif  // TANDEM_CAP_BLE
