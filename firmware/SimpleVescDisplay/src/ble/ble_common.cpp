#include "ble_common.h"
#include <NimBLEDevice.h>
#include "../config/app_config.h"
#include "../core/settings.h"

static bool s_init = false;
static char s_name[20] = {0};

const char* ble::deviceName() {
  if (cfg.bleName[0]) return cfg.bleName;  // user defined name (Settings -> Bluetooth)
  if (!s_name[0]) {
    const uint64_t mac = ESP.getEfuseMac();
    snprintf(s_name, sizeof(s_name), "VESC SVD %02X%02X", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  }
  return s_name;
}

bool ble::init() {
  if (s_init) return true;
  // Every other NimBLE call crashes on a stack that did not start, so callers must check this.
  if (!NimBLEDevice::init(deviceName())) {
    LOGF("[ble] stack FAILED to start, free heap %u\n", (unsigned)ESP.getFreeHeap());
    return false;
  }
  NimBLEDevice::setMTU(247);
  s_init = true;
  LOGF("[ble] stack started as '%s', free heap %u\n", deviceName(), (unsigned)ESP.getFreeHeap());
  return true;
}

bool ble::ready() { return s_init; }
