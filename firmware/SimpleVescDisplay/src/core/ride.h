#pragma once
#include <Arduino.h>

// Everything the UI shows, derived from raw VESC telemetry + user settings.
struct RideData {
  bool     connected = false;
  uint32_t seq = 0;              // increments whenever new telemetry was processed

  // live
  float    speedKmh = 0;         // always >= 0
  float    erpm = 0;
  float    motorRpm = 0;
  float    voltage = 0;
  float    battPct = -1;         // -1 = unknown
  float    currentIn = 0;        // battery current (sum of all VESCs)
  float    currentMotor = 0;     // phase current (sum of all VESCs)
  float    powerW = 0;
  float    dutyPct = 0;          // highest |duty| of all VESCs
  float    tempFet = 0;          // highest of all VESCs
  float    tempMotor = 0;
  uint8_t  fault = 0;
  uint8_t  faultVesc = 0;        // 1 = primary, 2 = secondary

  // trip / statistics (since boot or last trip reset)
  float    tripKm = 0;
  double   odoKm = 0;
  float    maxSpeedKmh = 0;
  float    avgSpeedKmh = 0;
  float    maxPowerW = 0;
  float    maxRegenW = 0;
  float    maxCurrentIn = 0;
  float    maxCurrentMotor = 0;
  float    minVoltage = 0;
  float    maxTempFet = 0;
  float    maxTempMotor = 0;
  float    whUsed = 0;
  float    whRegen = 0;
  float    ahUsed = 0;
  float    ahRegen = 0;
  float    whPerKm = -1;         // -1 = not enough data
  float    rangeKm = -1;
  uint32_t rideSec = 0;          // time spent moving
  uint32_t sessionSec = 0;       // time since boot
};

class Ride {
 public:
  void begin();
  void update(uint32_t nowMs);
  void resetTrip();
  void setOdo(double km);
  void saveOdo();             // also saves the trip when cfg.keepTrip is on
  void saveTrip();
  const RideData& data() const { return d_; }

 private:
  struct Counters {
    bool    init = false;
    int32_t tacho = 0;
    float   wh = 0, whc = 0, ah = 0, ahc = 0;
  };
  void accumulateEnergy(int idx, bool firstEver);
  void maybeSaveOdo(uint32_t now);

  RideData d_;
  Counters c_[2];
  uint32_t lastSeq_[2] = {0, 0};
  uint32_t lastSampleMs_ = 0;
  uint32_t bootMs_ = 0;
  uint32_t rideMs_ = 0;
  double   avgDistKm_ = 0;
  double   savedOdo_ = 0;
  uint32_t stoppedSince_ = 0;
  float    vFilt_ = 0;
  bool     firstEver_ = true;
  bool     tripRestored_ = false;  // trip loaded from flash: do not add the VESC counters again
};

extern Ride ride;

float batteryPercentFromCell(float cellVoltage, uint8_t battType);
float cellEmptyVoltage(uint8_t battType);
float cellMaxVoltage(uint8_t battType);
