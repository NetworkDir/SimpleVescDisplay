#pragma once
#include <Arduino.h>

// Nordic UART Service - the same GATT layout the VESC BLE modules / VESC Tool use.
#define NUS_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // central -> peripheral (write)
#define NUS_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // peripheral -> central (notify)

namespace ble {
bool        init();         // starts the NimBLE stack once
bool        ready();
const char* deviceName();   // "VESC SVD xxxx"
}
