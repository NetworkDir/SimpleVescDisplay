#include "ble_link.h"
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/stream_buffer.h>
#include "ble_common.h"
#include "../core/settings.h"
#include "../config/app_config.h"

static constexpr uint32_t SCAN_MS = 3000;
static constexpr uint32_t SCAN_MIN_HEAP = 30 * 1024;
static constexpr uint32_t VERIFY_MS = 6000;   // a new device must answer like a VESC within this time
static constexpr uint8_t  MAX_SKIP = 8;

static StreamBufferHandle_t        s_rx = nullptr;
static SemaphoreHandle_t           s_lock = nullptr;  // guards s_rxChr while (re)connecting
static NimBLEClient*               s_client = nullptr;
static NimBLERemoteCharacteristic* s_rxChr = nullptr;
static volatile BleLink::State     s_state = BleLink::State::IDLE;
static char                        s_peer[24] = {0};
static volatile bool               s_bindPending = false;
static uint8_t                     s_bindAddr[6];
static uint8_t                     s_bindType = 0;
static volatile bool               s_verified = false;  // a valid VESC packet came from the peer
static volatile int                s_lostReason = 0;    // set by the disconnect callback

// Devices that were tried and did not answer like a VESC (until the next restart).
static uint8_t s_skip[MAX_SKIP][6];
static uint8_t s_skipCount = 0;
// The device of the last failed connect: skipped for one scan round only.
static uint8_t s_lastFail[6];
static bool    s_lastFailValid = false;

static bool isSkipped(const uint8_t* a) {
  if (s_lastFailValid && memcmp(s_lastFail, a, 6) == 0) return true;
  for (uint8_t i = 0; i < s_skipCount; i++) {
    if (memcmp(s_skip[i], a, 6) == 0) return true;
  }
  return false;
}

static void addSkip(const uint8_t* a) {
  for (uint8_t i = 0; i < s_skipCount; i++) {
    if (memcmp(s_skip[i], a, 6) == 0) return;
  }
  memcpy(s_skip[s_skipCount % MAX_SKIP], a, 6);
  if (s_skipCount < MAX_SKIP) s_skipCount++;
}

// ---- scanning -------------------------------------------------------------------
// The scan keeps no device list (setMaxResults(0)): in a crowded place hundreds of
// phones, watches and headphones advertise, and storing every one of them used to
// eat the heap until the display rebooted. Only the best VESC candidate is kept.
struct Candidate {
  bool    valid;
  uint8_t addr[6];
  uint8_t type;
  int     rssi;
  char    name[24];
};
static Candidate s_best;
static int       s_candidates = 0;

class LinkScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    static const NimBLEUUID kNus(NUS_SERVICE_UUID);
    const std::string name = d->getName();
    // VESC modules advertise the Nordic UART service; some only show it in the scan
    // response or not at all, so a name with "VESC" counts too.
    const bool vesc = d->isAdvertisingService(kNus) || name.find("VESC") != std::string::npos ||
                      name.find("vesc") != std::string::npos;
    if (!vesc) return;
    const NimBLEAddress& a = d->getAddress();
    if (isSkipped(a.getVal())) return;
    s_candidates++;
    const int rssi = d->getRSSI();
    if (s_best.valid && rssi <= s_best.rssi) return;
    s_best.valid = true;
    memcpy(s_best.addr, a.getVal(), 6);
    s_best.type = a.getType();
    s_best.rssi = rssi;
    snprintf(s_best.name, sizeof(s_best.name), "%s", name.c_str());
  }
};
static LinkScanCallbacks s_scanCb;

static bool scanForVesc() {
  s_best.valid = false;
  s_candidates = 0;
  // NimBLE still allocates a short-lived entry per advertiser while scanning; with
  // little free heap (a big page open) wait instead of risking an out-of-memory abort.
  if (ESP.getFreeHeap() < SCAN_MIN_HEAP) {
    LOGF("[ble] scan postponed, free heap %u\n", (unsigned)ESP.getFreeHeap());
    return false;
  }
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&s_scanCb, false);
  scan->setMaxResults(0);  // callbacks only, nothing is stored
  scan->setActiveScan(true);
  scan->setInterval(97);
  scan->setWindow(67);
  scan->getResults(SCAN_MS, false);  // blocks this task until the scan has ended
  LOGF("[ble] scan: %d VESC candidates, free heap %u\n", s_candidates, (unsigned)ESP.getFreeHeap());
  return s_best.valid;
}

// ---- connection -------------------------------------------------------------------
static void notifyCb(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  if (s_rx) xStreamBufferSend(s_rx, data, len, 0);
}

class LinkClientCallbacks : public NimBLEClientCallbacks {
  // Runs in the NimBLE host task (4 KB stack): no printf here, the link task logs it.
  void onDisconnect(NimBLEClient*, int reason) override {
    // stop the UI loop from writing right away; the link task reconnects
    if (s_state == BleLink::State::CONNECTED) {
      s_state = BleLink::State::CONNECTING;
      s_lostReason = reason;
    }
  }
};
static LinkClientCallbacks s_clientCb;

static void dropCharacteristic() {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_rxChr = nullptr;
  xSemaphoreGive(s_lock);
}

static bool connectTo(const NimBLEAddress& addr) {
  s_state = BleLink::State::CONNECTING;
  snprintf(s_peer, sizeof(s_peer), "%s", addr.toString().c_str());
  if (!s_client) {
    s_client = NimBLEDevice::createClient();
    if (!s_client) return false;
    s_client->setClientCallbacks(&s_clientCb, false);
    s_client->setConnectTimeout(5000);
  }
  if (!s_client->connect(addr)) {
    LOGF("[ble] connect to %s failed (busy with the phone? out of range?)\n", s_peer);
    return false;
  }

  NimBLERemoteService* svc = s_client->getService(NUS_SERVICE_UUID);
  NimBLERemoteCharacteristic* rx = svc ? svc->getCharacteristic(NUS_RX_UUID) : nullptr;
  NimBLERemoteCharacteristic* tx = svc ? svc->getCharacteristic(NUS_TX_UUID) : nullptr;
  if (!rx || !tx || !tx->canNotify() || !tx->subscribe(true, notifyCb)) {
    LOGF("[ble] %s: %s\n", s_peer, !svc ? "no VESC UART service" : "cannot subscribe (pairing PIN enabled?)");
    s_client->disconnect();
    return false;
  }
  s_verified = false;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_rxChr = rx;
  xSemaphoreGive(s_lock);
  s_state = BleLink::State::CONNECTED;
  LOGF("[ble] connected to %s (MTU %u)\n", s_peer, s_client->getMTU());
  return true;
}

static void disconnectPeer() {
  s_state = BleLink::State::CONNECTING;
  dropCharacteristic();
  if (s_client && s_client->isConnected()) s_client->disconnect();
}

// Connection manager: scan / connect / re-connect without blocking the UI loop.
static void linkTask(void*) {
  for (;;) {
    if (s_state == BleLink::State::CONNECTED && s_client && s_client->isConnected()) {
      vTaskDelay(pdMS_TO_TICKS(300));
      continue;
    }
    if (s_lostReason) {
      LOGF("[ble] VESC link lost (reason %d)\n", s_lostReason);
      s_lostReason = 0;
    }
    dropCharacteristic();

    if (cfg.bleAddrValid) {
      // the bound VESC: only this one, until "Forget" in the settings
      if (connectTo(NimBLEAddress(cfg.bleAddr, cfg.bleAddrType))) continue;
    } else {
      s_state = BleLink::State::SCANNING;
      s_peer[0] = 0;
      if (scanForVesc()) {
        const Candidate c = s_best;
        LOGF("[ble] trying '%s' rssi %d\n", c.name, c.rssi);
        if (connectTo(NimBLEAddress(c.addr, c.type))) {
          // Bind only to a device that really talks the VESC protocol: other Nordic UART
          // gadgets advertise the same service and must not be remembered.
          const uint32_t t0 = millis();
          while (!s_verified && s_state == BleLink::State::CONNECTED && millis() - t0 < VERIFY_MS)
            vTaskDelay(pdMS_TO_TICKS(100));
          if (s_verified) {
            memcpy(s_bindAddr, c.addr, 6);
            s_bindType = c.type;
            s_bindPending = true;
            LOGF("[ble] '%s' answers like a VESC - bound\n", c.name);
            continue;
          }
          LOGF("[ble] '%s' does not answer like a VESC - skipped\n", c.name);
          disconnectPeer();
          addSkip(c.addr);
        } else {
          // busy (phone connected?) or out of range: give the other candidates a turn first
          memcpy(s_lastFail, c.addr, 6);
          s_lastFailValid = true;
        }
      } else {
        s_lastFailValid = false;  // nothing else found: the failed one may be tried again
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

bool BleLink::begin() {
  s_rx = xStreamBufferCreate(2048, 1);
  s_lock = xSemaphoreCreateMutex();
  if (!s_rx || !s_lock) return false;
  if (!ble::init()) return false;
  s_state = State::SCANNING;
  return xTaskCreatePinnedToCore(linkTask, "ble_link", 8192, nullptr, 1, nullptr, 0) == pdPASS;
}

void BleLink::poll(uint32_t) {
  if (!s_rx) return;  // begin() failed
  if (s_bindPending) {  // remember the VESC we connected to (settings are owned by this task)
    s_bindPending = false;
    memcpy(cfg.bleAddr, s_bindAddr, 6);
    cfg.bleAddrType = s_bindType;
    cfg.bleAddrValid = 1;
    settingsSave();
  }
  for (int guard = 0; guard < 8; guard++) {
    size_t n = xStreamBufferReceive(s_rx, chunk_, sizeof(chunk_), 0);
    if (!n) break;
    for (size_t i = 0; i < n; i++) {
      if (parser_.feed(chunk_[i])) {
        s_verified = true;
        dispatch(VESC_TARGET_LOCAL, parser_.payload(), parser_.length());
      }
    }
  }
}

bool BleLink::send(uint8_t targetId, const uint8_t* payload, size_t len) {
  if (s_state != State::CONNECTED) return false;
  static uint8_t body[vesc::MAX_PAYLOAD];
  static uint8_t frame[vesc::MAX_PAYLOAD + 8];
  if (targetId != VESC_TARGET_LOCAL) {
    if (len + 2 > sizeof(body)) return false;
    body[0] = vesc::COMM_FORWARD_CAN;
    body[1] = targetId;
    memcpy(body + 2, payload, len);
    payload = body;
    len += 2;
  }
  const size_t n = vesc::buildFrame(payload, len, frame, sizeof(frame));
  if (!n) return false;

  if (xSemaphoreTake(s_lock, 0) != pdTRUE) return false;
  bool ok = s_rxChr != nullptr && s_client != nullptr && s_client->isConnected();
  if (ok) {
    const uint16_t mtu = s_client->getMTU();
    size_t chunk = mtu > 23 ? mtu - 3 : 20;
    if (chunk > 244) chunk = 244;
    for (size_t off = 0; ok && off < n; off += chunk) {
      size_t part = (n - off) < chunk ? (n - off) : chunk;
      ok = s_rxChr->writeValue(frame + off, part, false);
    }
  }
  xSemaphoreGive(s_lock);
  return ok;
}

BleLink::State BleLink::state() { return s_state; }

const char* BleLink::stateName() {
  switch (s_state) {
    case State::SCANNING:   return "scanning";
    case State::CONNECTING: return "connecting";
    case State::CONNECTED:  return "connected";
    default:                return "off";
  }
}

const char* BleLink::peerAddress() { return s_peer; }
