#pragma once
#include <Arduino.h>

// Out-of-memory protection.
//
// LVGL shares the heap with Bluetooth, the SD card and the VESC links. An LVGL
// allocation that fails used to end in an assert (= reboot). Now LVGL allocates
// through this module: a reserve block is kept aside and released when an
// allocation would fail, and the UI is told to free memory (close the open page).
// The reserve is taken back once enough memory is free again.
namespace memguard {
void     begin();                    // call first in setup()
void     service(uint32_t nowMs);    // call from loop()
bool     takeLowMemoryEvent();       // true once after the reserve had to be used
bool     roomForPage();              // enough free heap to build another page?
bool     roomForDialog();            // ... for a keyboard / keypad / chooser?
uint32_t freeHeap();                 // bytes, without the reserve
uint32_t largestBlock();             // bytes
uint32_t minFreeHeap();              // lowest free heap since boot
uint32_t lowMemoryCount();           // how often the reserve was needed since boot

// Why the display restarted last time (power on, brownout, crash, ...).
const char* resetReasonText();
bool        resetWasAbnormal();      // crash / watchdog / brownout
}  // namespace memguard
