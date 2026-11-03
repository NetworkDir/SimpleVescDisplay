// =============================================================================
//  SimpleVescDisplay v2 - LVGL touch dashboard for VESC based vehicles
//
//  Boards : ESP32-2432S028R (2.8")  and  ESP32-3248S035R / ESP32-3248S035C (3.5")
//           -> select the board in src/config/board_config.h (Arduino IDE)
//              or with the PlatformIO environment.
//  Links  : UART (default), CAN (TWAI + transceiver) or Bluetooth LE,
//           optional BLE bridge so VESC Tool can connect through the display.
//
//  Based on the original SimpleVescDisplay (github.com/NetworkDir/SimpleVescDisplay)
//  which itself is based on github.com/Gh0513d/SVD.
// =============================================================================
#include <Arduino.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <esp_bt.h>
#include "src/config/board_config.h"
#include "src/config/app_config.h"
#include "src/core/settings.h"
#include "src/core/ride.h"
#include "src/core/alerts.h"
#include "src/core/sdlog.h"
#include "src/core/ride_modes.h"
#include "src/vesc/telemetry.h"
#include "src/hal/display.h"
#include "src/hal/buzzer.h"
#include "src/hal/memguard.h"
#include "src/ui/ui.h"

// LVGL rendering, the VESC protocol and the SD log all run in loop(): 12 KB instead
// of the default 8 KB leaves a safe margin (a stack overflow is an instant reboot).
SET_LOOP_TASK_STACK_SIZE(12 * 1024);

static void logHeap(const char* where) {
  LOGF("[heap] %s: free %u KB, largest block %u KB, min free %u KB, loop stack left %u B\n", where,
       (unsigned)(memguard::freeHeap() / 1024), (unsigned)(memguard::largestBlock() / 1024),
       (unsigned)(memguard::minFreeHeap() / 1024), (unsigned)uxTaskGetStackHighWaterMark(nullptr));
}

void setup() {
  Serial.begin(115200);
  LOGF("\n\n%s v%s - %s\n", FW_NAME, FW_VERSION, BOARD_NAME);
  memguard::begin();

  settingsLoad();

  // The BLE library keeps ~100 KB reserved for the Bluetooth controller. Give it
  // back to the heap when Bluetooth is not used (re-enabling BLE needs a restart).
  const bool bleUsed = (cfg.commMode == COMM_BLE) || (cfg.bleBridge && !cfg.demoMode);
  if (!bleUsed) esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);

  display::begin();

  char footer[64];
  snprintf(footer, sizeof(footer), "v%s  %s", FW_VERSION, BOARD_NAME);
  display::showSplash(footer);

  // Touch calibration: automatically on the first boot of a resistive panel,
  // or whenever the screen is touched while the logo is shown.
  bool calibrate = !cfg.touchCalValid && display::touchIsResistive();
  for (uint32_t t0 = millis(); !calibrate && millis() - t0 < 3500;) {
    if (display::touchPressed()) calibrate = true;
    delay(20);
  }
  if (calibrate) display::calibrateTouch();

  ride.begin();
  telemetry.begin();
  alertsBegin();
  logHeap("before LVGL");

  display::lvglBegin();
  uiBegin();
  logHeap("UI ready");

  if (memguard::resetWasAbnormal()) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Restarted after: %s", memguard::resetReasonText());
    uiToast(msg, 0xFFC81E, 8000);
  }
  if (cfg.buzzer) buzzerPlay(BEEP_BOOT);
}

void loop() {
  const uint32_t now = millis();

  telemetry.update(now);       // VESC link (non-blocking)
  ride.update(now);            // speed, distance, energy, statistics
  rideModesUpdate(now);        // ECO / DRIVE / SPORT / SPORT+ limits in the VESC
  alertsUpdate(now);           // warnings, LED, beeper
  sdlogUpdate(now);            // CSV logging
  display::serviceBacklight(now);
  settingsService(now);        // deferred settings save
  memguard::service(now);      // out-of-memory reserve
  uiUpdate(now);
  lv_timer_handler();

#if SVD_DEBUG
  static uint32_t lastHeapLog = 0;
  if (now - lastHeapLog > 60000) {
    lastHeapLog = now;
    logHeap("runtime");
  }
#endif
  delay(2);
}
