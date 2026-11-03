#pragma once
#include <stdint.h>

enum CommMode : uint8_t { COMM_UART = 0, COMM_CAN = 1, COMM_BLE = 2 };

// All user settings. Stored as one blob in NVS ("svd" namespace).
// Keep fields fixed-size; bump SETTINGS_VERSION when the layout changes.
struct Settings {
  uint16_t magic;
  uint16_t version;

  // Connection
  uint8_t  commMode;        // CommMode
  uint8_t  canBitrate;      // 0=125k 1=250k 2=500k 3=1M
  uint8_t  canVescId;       // 255 = auto detect
  uint8_t  canOwnId;
  uint8_t  secondVescId;    // 255 = disabled
  uint8_t  pollHz;
  uint32_t uartBaud;

  // Vehicle
  uint8_t  motorPoles;
  uint8_t  batteryCells;
  uint16_t wheelDiameterMm;
  uint16_t gearRatioX100;
  uint16_t batteryAhX10;

  // Units & warnings
  uint8_t  speedUnit;       // 0 km/h, 1 mph
  uint8_t  tempUnit;        // 0 C, 1 F
  uint8_t  warnSpeed;       // km/h
  uint8_t  fetTempWarn;
  uint8_t  fetTempCrit;
  uint8_t  motorTempWarn;
  uint8_t  motorTempCrit;
  uint8_t  warnDutyPct;
  uint8_t  warnBattPct;
  uint8_t  buzzer;
  uint8_t  led;

  // Display
  uint8_t  autoBrightness;
  uint8_t  brightness;      // manual level / max level in auto mode
  uint8_t  flip;            // rotate 180 degrees
  uint8_t  invertColors;
  uint8_t  swapRB;          // needs restart
  uint8_t  autoReturn;      // return to dash while riding

  // Misc
  uint8_t  demoMode;
  uint8_t  sdLog;

  // Bluetooth LE
  uint8_t  bleBridge;       // act as a VESC BLE module for VESC Tool (UART/CAN modes)
  uint8_t  blePairing;      // bridge requires the 6 digit PIN shown on screen
  uint8_t  bleAddrValid;    // BLE mode: VESC BLE module bound to this display
  uint8_t  bleAddrType;
  uint8_t  bleAddr[6];
  uint32_t blePasskey;

  // Touch calibration (LovyanGFX format)
  uint8_t  touchCalValid;
  uint8_t  reserved0;
  uint16_t touchCal[8];

  // Battery / speed
  uint8_t  battType;        // BattType
  int8_t   speedCorrPct;    // -20 .. +20 % speed & distance correction

  // Dashboard
  uint8_t  dashCell[5];     // DashCell for the 5 footer cells
  uint8_t  speedDecimals;   // show one decimal on the speed
  uint8_t  startPage;       // page shown after boot
  uint8_t  gaugeSweep;      // 0 -> 99 -> 0 "hello" sweep of the gauges at start-up
  uint16_t powerScaleW;     // full scale of the power bar, 0 = auto

  // Display / sound
  uint8_t  autoMinBright;   // lowest brightness in auto mode
  uint16_t ldrDark;         // light sensor reading that counts as "dark"
  uint8_t  dimMinutes;      // dim the screen when standing still, 0 = off
  uint8_t  bootLogo;
  uint8_t  touchSound;
  uint8_t  speedBeep;       // beep when the speed warning starts

  // Data
  uint8_t  keepTrip;        // keep trip values across restarts

  char     bleName[20];     // empty = "VESC SVD xxxx"

  // Ride modes (temporary VESC motor limits, never stored in the VESC)
  uint8_t  rideModes;       // feature on/off
  uint8_t  rideMode;        // RideMode currently selected
  uint8_t  modeCurrent[4];  // motor current % (10..100)
  uint8_t  modeBrake[4];    // regen / brake current % (10..100)
  uint8_t  modeSpeed[4];    // speed limit km/h, 0 = the VESC's own limit
  uint8_t  modeTcs[4];      // traction control on (needs VESC FW 7+, dual motor ADC app)
  uint8_t  tcsControl;      // send traction control changes at all
  uint8_t  modeTheme;       // UI colour follows the ride mode
};

enum RideMode : uint8_t { MODE_ECO = 0, MODE_DRIVE, MODE_SPORT, MODE_SPORT_PLUS, MODE_COUNT };

enum BattType : uint8_t { BATT_LIION = 0, BATT_LIFEPO4 = 1 };

enum DashCell : uint8_t {
  CELL_ESC_TEMP, CELL_MOTOR_TEMP, CELL_TRIP, CELL_ODO, CELL_BATT_A, CELL_PHASE_A, CELL_POWER,
  CELL_VOLTAGE, CELL_BATT_PCT, CELL_DUTY, CELL_RANGE, CELL_EFFICIENCY, CELL_WH_USED, CELL_AH_USED,
  CELL_RIDE_TIME, CELL_AVG_SPEED, CELL_MAX_SPEED,
  CELL_COUNT
};

extern Settings cfg;

void   settingsDefaults(Settings& s);
void   settingsLoad();
void   settingsSave();
void   settingsMarkDirty();               // deferred save (see settingsService)
void   settingsService(uint32_t nowMs);   // call from loop()
void   settingsFactoryReset();            // defaults + keeps the odometer

// Odometer is stored separately so a settings reset never loses it.
double odoLoadKm();
void   odoSaveKm(double km);

// Trip values that survive a restart when cfg.keepTrip is on
struct TripStore {
  float    tripKm, whUsed, whRegen, ahUsed, ahRegen, maxSpeedKmh, maxPowerW, maxRegenW;
  float    maxCurrentIn, maxCurrentMotor, minVoltage, maxTempFet, maxTempMotor;
  double   avgDistKm;
  uint32_t rideMs;
};
bool   tripLoad(TripStore& t);
void   tripSave(const TripStore& t);
void   tripClear();

uint32_t canBitrateKbps(uint8_t idx);
