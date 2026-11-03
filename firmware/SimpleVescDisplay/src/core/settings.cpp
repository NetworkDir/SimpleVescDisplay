#include "settings.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include "../config/app_config.h"
#include "../config/board_config.h"

static constexpr uint16_t SETTINGS_MAGIC = 0x5644;  // "VD"
static constexpr uint16_t SETTINGS_VERSION = 1;
static const char* NVS_NS = "svd";

Settings cfg;

static bool     s_dirty = false;
static uint32_t s_dirtySince = 0;

void settingsDefaults(Settings& s) {
  memset(&s, 0, sizeof(s));
  s.magic = SETTINGS_MAGIC;
  s.version = SETTINGS_VERSION;

  s.commMode = DEF_COMM_MODE;
  s.canBitrate = DEF_CAN_BITRATE;
  s.canVescId = DEF_CAN_VESC_ID;
  s.canOwnId = DEF_CAN_OWN_ID;
  s.secondVescId = DEF_SECOND_VESC_ID;
  s.pollHz = DEF_POLL_HZ;
  s.uartBaud = DEF_UART_BAUD;

  s.motorPoles = DEF_MOTOR_POLES;
  s.batteryCells = DEF_BATTERY_CELLS;
  s.wheelDiameterMm = DEF_WHEEL_DIAMETER_MM;
  s.gearRatioX100 = DEF_GEAR_RATIO_X100;
  s.batteryAhX10 = DEF_BATTERY_AH_X10;

  s.speedUnit = 0;
  s.tempUnit = 0;
  s.warnSpeed = DEF_WARN_SPEED;
  s.fetTempWarn = DEF_FET_TEMP_WARN;
  s.fetTempCrit = DEF_FET_TEMP_CRIT;
  s.motorTempWarn = DEF_MOTOR_TEMP_WARN;
  s.motorTempCrit = DEF_MOTOR_TEMP_CRIT;
  s.warnDutyPct = DEF_WARN_DUTY_PCT;
  s.warnBattPct = DEF_WARN_BATT_PCT;
  s.buzzer = 1;
  s.led = 1;

  s.autoBrightness = 1;
  s.brightness = DEF_BRIGHTNESS;
  s.flip = 0;
  s.invertColors = 0;
  s.swapRB = 0;
  s.autoReturn = 1;

  s.demoMode = 0;
  s.sdLog = 0;
  s.touchCalValid = 0;

  s.bleBridge = 0;
  s.blePairing = 1;
  s.bleAddrValid = 0;
  s.blePasskey = 100000 + (esp_random() % 900000);

  s.battType = BATT_LIION;
  s.speedCorrPct = 0;
  const uint8_t cells[5] = {CELL_ESC_TEMP, CELL_MOTOR_TEMP, CELL_TRIP, CELL_BATT_A, CELL_PHASE_A};
  memcpy(s.dashCell, cells, sizeof(cells));  // same cells as the original firmware
  s.speedDecimals = 0;
  s.startPage = 0;
  s.gaugeSweep = 1;
  s.powerScaleW = 0;
  s.autoMinBright = BRIGHTNESS_MIN_AUTO;
  s.ldrDark = LDR_RAW_DARK;
  s.dimMinutes = 0;
  s.bootLogo = 1;
  s.touchSound = 0;
  s.speedBeep = 1;
  s.keepTrip = 0;
  s.bleName[0] = 0;

  // ECO / DRIVE / SPORT / SPORT+  (brake stays at 100 % everywhere on purpose)
  static const uint8_t cur[4] = {50, 75, 100, 100};
  static const uint8_t spd[4] = {25, 0, 0, 0};
  static const uint8_t tcs[4] = {1, 1, 1, 0};
  s.rideModes = 0;            // off until the rider enables it
  s.rideMode = MODE_SPORT;
  for (int i = 0; i < 4; i++) {
    s.modeCurrent[i] = cur[i];
    s.modeBrake[i] = 100;
    s.modeSpeed[i] = spd[i];
    s.modeTcs[i] = tcs[i];
  }
  s.tcsControl = 0;
  s.modeTheme = 1;
}

// Keeps values inside sane ranges (protects against a corrupted blob).
static void sanitize(Settings& s) {
  if (s.commMode > COMM_BLE) s.commMode = COMM_UART;
  if (s.blePasskey < 100000 || s.blePasskey > 999999) s.blePasskey = 100000 + (esp_random() % 900000);
  if (s.canBitrate > 3) s.canBitrate = DEF_CAN_BITRATE;
  if (s.pollHz < 2 || s.pollHz > 50) s.pollHz = DEF_POLL_HZ;
  if (s.uartBaud < 9600 || s.uartBaud > 2000000) s.uartBaud = DEF_UART_BAUD;
  if (s.motorPoles < 2 || (s.motorPoles & 1)) s.motorPoles = DEF_MOTOR_POLES;
  if (s.batteryCells < 3 || s.batteryCells > 30) s.batteryCells = DEF_BATTERY_CELLS;
  if (s.wheelDiameterMm < 50 || s.wheelDiameterMm > 2000) s.wheelDiameterMm = DEF_WHEEL_DIAMETER_MM;
  if (s.gearRatioX100 < 10 || s.gearRatioX100 > 5000) s.gearRatioX100 = DEF_GEAR_RATIO_X100;
  if (s.batteryAhX10 > 5000) s.batteryAhX10 = DEF_BATTERY_AH_X10;
  if (s.brightness < 10) s.brightness = 10;
  if (s.battType > BATT_LIFEPO4) s.battType = BATT_LIION;
  if (s.speedCorrPct < -20 || s.speedCorrPct > 20) s.speedCorrPct = 0;
  for (auto& c : s.dashCell) if (c >= CELL_COUNT) c = CELL_TRIP;
  if (s.startPage > 3) s.startPage = 0;
  if (s.ldrDark < LDR_RAW_BRIGHT + 50 || s.ldrDark > 4095) s.ldrDark = LDR_RAW_DARK;
  s.bleName[sizeof(s.bleName) - 1] = 0;
  if (s.rideMode >= MODE_COUNT) s.rideMode = MODE_SPORT;
  for (int i = 0; i < 4; i++) {
    if (s.modeCurrent[i] < 10 || s.modeCurrent[i] > 100) s.modeCurrent[i] = 100;
    if (s.modeBrake[i] < 10 || s.modeBrake[i] > 100) s.modeBrake[i] = 100;
  }
}

void settingsLoad() {
  Preferences p;
  Settings tmp;
  bool ok = false;
  if (p.begin(NVS_NS, true)) {
    if (p.getBytesLength("cfg") == sizeof(Settings)) {
      p.getBytes("cfg", &tmp, sizeof(tmp));
      ok = (tmp.magic == SETTINGS_MAGIC && tmp.version == SETTINGS_VERSION);
    }
    p.end();
  }
  if (ok) {
    cfg = tmp;
    sanitize(cfg);
    LOGF("[cfg] loaded from NVS\n");
  } else {
    settingsDefaults(cfg);
    LOGF("[cfg] using defaults\n");
    settingsSave();
  }
}

void settingsSave() {
  Preferences p;
  if (p.begin(NVS_NS, false)) {
    p.putBytes("cfg", &cfg, sizeof(cfg));
    p.end();
  }
  s_dirty = false;
  LOGF("[cfg] saved\n");
}

void settingsMarkDirty() {
  s_dirty = true;
  s_dirtySince = millis();
}

void settingsService(uint32_t now) {
  if (s_dirty && now - s_dirtySince > 3000) settingsSave();
}

void settingsFactoryReset() {
  settingsDefaults(cfg);
  settingsSave();
}

// ---------------------------------------------------------------------------
//  Odometer
// ---------------------------------------------------------------------------

// The v1 firmware stored the total distance as a float at address 0 of the
// Arduino EEPROM emulation, which lives in NVS namespace "eeprom", key "eeprom".
static bool readLegacyOdo(float& km) {
  Preferences p;
  bool ok = false;
  if (p.begin("eeprom", true)) {
    size_t len = p.getBytesLength("eeprom");
    if (len >= sizeof(float)) {
      uint8_t buf[16];
      size_t n = len < sizeof(buf) ? len : sizeof(buf);
      p.getBytes("eeprom", buf, n);
      memcpy(&km, buf, sizeof(float));
      ok = isfinite(km) && km > 0.0f && km < 1000000.0f;
    }
    p.end();
  }
  return ok;
}

double odoLoadKm() {
  Preferences p;
  double km = 0;
  bool have = false;
  if (p.begin(NVS_NS, true)) {
    if (p.isKey("odo")) {
      km = p.getDouble("odo", 0.0);
      have = true;
    }
    p.end();
  }
  if (!have) {
    float legacy = 0;
    if (readLegacyOdo(legacy)) {
      km = legacy;
      LOGF("[odo] imported %.1f km from the v1 firmware\n", km);
    }
    odoSaveKm(km);
  }
  if (!isfinite(km) || km < 0) km = 0;
  return km;
}

void odoSaveKm(double km) {
  Preferences p;
  if (p.begin(NVS_NS, false)) {
    p.putDouble("odo", km);
    p.end();
  }
}

bool tripLoad(TripStore& t) {
  Preferences p;
  bool ok = false;
  if (p.begin(NVS_NS, true)) {
    ok = p.getBytesLength("trip") == sizeof(TripStore) && p.getBytes("trip", &t, sizeof(t)) == sizeof(t);
    p.end();
  }
  return ok;
}

void tripSave(const TripStore& t) {
  Preferences p;
  if (p.begin(NVS_NS, false)) {
    p.putBytes("trip", &t, sizeof(t));
    p.end();
  }
}

void tripClear() {
  Preferences p;
  if (p.begin(NVS_NS, false)) {
    p.remove("trip");
    p.end();
  }
}

uint32_t canBitrateKbps(uint8_t idx) {
  static const uint16_t rates[] = {125, 250, 500, 1000};
  return idx < 4 ? rates[idx] : 500;
}
