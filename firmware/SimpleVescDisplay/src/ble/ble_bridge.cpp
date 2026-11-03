#include "ble_bridge.h"
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include "ble_common.h"
#include "../core/settings.h"
#include "../config/app_config.h"

BleBridge bleBridge;

// NimBLE callbacks run in the BLE host task; data crosses to the loop task
// through two FreeRTOS stream buffers (single producer / single consumer).
static StreamBufferHandle_t s_rx = nullptr;   // VESC Tool -> display
static StreamBufferHandle_t s_tx = nullptr;   // display -> VESC Tool (framed bytes)
static NimBLEServer*         s_server = nullptr;
static NimBLECharacteristic* s_txChr = nullptr;
static volatile bool     s_connected = false;
static volatile bool     s_authOk = false;
static volatile bool     s_pinShown = false;
static volatile uint16_t s_conn = 0;
static volatile uint16_t s_mtu = 23;

static bool secureEnough(NimBLEConnInfo& ci) {
  return !cfg.blePairing || (ci.isEncrypted() && ci.isAuthenticated());
}

class BridgeServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& ci) override {
    s_conn = ci.getConnHandle();
    s_mtu = ci.getMTU();
    s_connected = true;
    s_authOk = !cfg.blePairing;
    if (cfg.blePairing) NimBLEDevice::startSecurity(ci.getConnHandle());
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    s_connected = false;
    s_authOk = false;
    s_pinShown = false;
  }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { s_mtu = mtu; }
  uint32_t onPassKeyDisplay() override {
    s_pinShown = true;
    return cfg.blePasskey;
  }
  void onAuthenticationComplete(NimBLEConnInfo& ci) override {
    s_pinShown = false;
    if (!secureEnough(ci)) {
      s_server->disconnect(ci.getConnHandle());
      return;
    }
    s_authOk = true;
  }
};

class BridgeRxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& ci) override {
    if (!secureEnough(ci)) return;  // unauthenticated data is dropped
    NimBLEAttValue v = c->getValue();
    if (s_rx) xStreamBufferSend(s_rx, v.data(), v.length(), 0);
  }
};

bool BleBridge::begin() {
  if (active_) return true;
  s_rx = xStreamBufferCreate(2048, 1);
  s_tx = xStreamBufferCreate(4096, 1);
  if (!s_rx || !s_tx) return false;
  if (!ble::init()) return false;

  if (cfg.blePairing) {
    NimBLEDevice::setSecurityAuth(true, true, true);  // bonding, MITM, secure connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    NimBLEDevice::setSecurityPasskey(cfg.blePasskey);
  }

  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(new BridgeServerCallbacks());
  s_server->advertiseOnDisconnect(true);

  NimBLEService* svc = s_server->createService(NUS_SERVICE_UUID);
  NimBLECharacteristic* rx =
      svc->createCharacteristic(NUS_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(new BridgeRxCallbacks());
  s_txChr = svc->createCharacteristic(NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  svc->start();  // required by NimBLE-Arduino < 2.3, a no-op (deprecated) in newer versions
#pragma GCC diagnostic pop

  // VESC Tool lists devices that advertise the NUS service UUID.
  // flags + 128 bit UUID fill the advertisement, the name goes into the scan response.
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.addServiceUUID(NUS_SERVICE_UUID);
  adv->setAdvertisementData(advData);
  NimBLEAdvertisementData scanData;
  scanData.setName(ble::deviceName());
  adv->setScanResponseData(scanData);
  adv->enableScanResponse(true);
  active_ = adv->start();
  LOGF("[ble] VESC Tool bridge %s (pairing %s)\n", active_ ? "advertising" : "FAILED",
       cfg.blePairing ? "on" : "off");
  return active_;
}

bool BleBridge::connected() const { return active_ && s_connected && s_authOk; }
bool BleBridge::pinRequested() const { return active_ && s_connected && s_pinShown; }

void BleBridge::forgetBonds() {
  if (ble::ready()) NimBLEDevice::deleteAllBonds();
}

bool BleBridge::readPacket(const uint8_t*& payload, size_t& len) {
  if (!active_) return false;
  for (int guard = 0; guard < 8; guard++) {
    if (chunkPos_ >= chunkLen_) {
      chunkLen_ = xStreamBufferReceive(s_rx, chunk_, sizeof(chunk_), 0);
      chunkPos_ = 0;
      if (!chunkLen_) return false;
      rxBytes_ += chunkLen_;
    }
    while (chunkPos_ < chunkLen_) {
      if (parser_.feed(chunk_[chunkPos_++])) {
        payload = parser_.payload();
        len = parser_.length();
        return true;
      }
    }
  }
  return false;
}

void BleBridge::sendPacket(const uint8_t* payload, size_t len) {
  if (!connected()) return;
  static uint8_t frame[vesc::MAX_PAYLOAD + 8];
  size_t n = vesc::buildFrame(payload, len, frame, sizeof(frame));
  if (n && xStreamBufferSpacesAvailable(s_tx) >= n) xStreamBufferSend(s_tx, frame, n, 0);
}

void BleBridge::service(uint32_t) {
  if (!active_) return;
  const bool conn = connected();
  if (conn != wasConnected_) {
    wasConnected_ = conn;
    // fresh session: drop leftovers of the previous one
    xStreamBufferReset(s_tx);
    xStreamBufferReset(s_rx);
    parser_.reset();
    chunkLen_ = chunkPos_ = pendLen_ = 0;
    LOGF("[ble] VESC Tool %s\n", conn ? "connected" : "disconnected");
  }
  if (!conn) return;

  size_t chunk = s_mtu > 3 ? s_mtu - 3 : 20;
  if (chunk > sizeof(pend_)) chunk = sizeof(pend_);
  for (int i = 0; i < 12; i++) {
    if (!pendLen_) {
      pendLen_ = xStreamBufferReceive(s_tx, pend_, chunk, 0);
      if (!pendLen_) break;
    }
    if (!s_txChr->notify(pend_, pendLen_, s_conn)) break;  // stack busy - retry next loop
    txBytes_ += pendLen_;
    pendLen_ = 0;
  }
}
