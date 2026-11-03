#include "ride.h"
#include <math.h>
#include "settings.h"
#include "../vesc/telemetry.h"
#include "../config/app_config.h"

Ride ride;

// Resting voltage curves per cell, 0 %, 5 % ... 100 %
static const float kCurveLiIon[21] = {3.27f, 3.61f, 3.69f, 3.71f, 3.73f, 3.75f, 3.77f, 3.79f, 3.80f, 3.82f, 3.84f,
                                      3.85f, 3.87f, 3.91f, 3.95f, 3.98f, 4.02f, 4.08f, 4.11f, 4.15f, 4.20f};
static const float kCurveLfp[21] = {2.50f, 2.90f, 3.00f, 3.10f, 3.18f, 3.20f, 3.22f, 3.24f, 3.25f, 3.26f, 3.27f,
                                    3.28f, 3.29f, 3.30f, 3.31f, 3.32f, 3.33f, 3.34f, 3.36f, 3.40f, 3.45f};

float batteryPercentFromCell(float v, uint8_t type) {
  const float* volts = type == BATT_LIFEPO4 ? kCurveLfp : kCurveLiIon;
  if (v <= volts[0]) return 0;
  if (v >= volts[20]) return 100;
  for (int i = 1; i < 21; i++) {
    if (v < volts[i]) {
      float f = (v - volts[i - 1]) / (volts[i] - volts[i - 1]);
      return (i - 1 + f) * 5.0f;
    }
  }
  return 100;
}

float cellEmptyVoltage(uint8_t type) { return type == BATT_LIFEPO4 ? 2.6f : 3.0f; }
float cellMaxVoltage(uint8_t type) { return type == BATT_LIFEPO4 ? 3.70f : 4.25f; }

void Ride::begin() {
  bootMs_ = millis();
  d_.odoKm = savedOdo_ = odoLoadKm();
  LOGF("[ride] odometer %.2f km\n", d_.odoKm);

  TripStore t;
  if (cfg.keepTrip && tripLoad(t)) {
    d_.tripKm = t.tripKm;
    d_.whUsed = t.whUsed;
    d_.whRegen = t.whRegen;
    d_.ahUsed = t.ahUsed;
    d_.ahRegen = t.ahRegen;
    d_.maxSpeedKmh = t.maxSpeedKmh;
    d_.maxPowerW = t.maxPowerW;
    d_.maxRegenW = t.maxRegenW;
    d_.maxCurrentIn = t.maxCurrentIn;
    d_.maxCurrentMotor = t.maxCurrentMotor;
    d_.minVoltage = t.minVoltage;
    d_.maxTempFet = t.maxTempFet;
    d_.maxTempMotor = t.maxTempMotor;
    avgDistKm_ = t.avgDistKm;
    rideMs_ = t.rideMs;
    tripRestored_ = true;
    LOGF("[ride] trip restored: %.2f km\n", d_.tripKm);
  }
}

void Ride::saveTrip() {
  if (!cfg.keepTrip) return;
  TripStore t;
  t.tripKm = d_.tripKm;
  t.whUsed = d_.whUsed;
  t.whRegen = d_.whRegen;
  t.ahUsed = d_.ahUsed;
  t.ahRegen = d_.ahRegen;
  t.maxSpeedKmh = d_.maxSpeedKmh;
  t.maxPowerW = d_.maxPowerW;
  t.maxRegenW = d_.maxRegenW;
  t.maxCurrentIn = d_.maxCurrentIn;
  t.maxCurrentMotor = d_.maxCurrentMotor;
  t.minVoltage = d_.minVoltage;
  t.maxTempFet = d_.maxTempFet;
  t.maxTempMotor = d_.maxTempMotor;
  t.avgDistKm = avgDistKm_;
  t.rideMs = rideMs_;
  tripSave(t);
}

void Ride::resetTrip() {
  d_.tripKm = 0;
  d_.maxSpeedKmh = d_.avgSpeedKmh = 0;
  d_.maxPowerW = d_.maxRegenW = 0;
  d_.maxCurrentIn = d_.maxCurrentMotor = 0;
  d_.minVoltage = d_.voltage;
  d_.maxTempFet = d_.tempFet;
  d_.maxTempMotor = d_.tempMotor;
  d_.whUsed = d_.whRegen = d_.ahUsed = d_.ahRegen = 0;
  d_.whPerKm = d_.rangeKm = -1;
  rideMs_ = 0;
  avgDistKm_ = 0;
  tripClear();
  saveOdo();
}

void Ride::setOdo(double km) {
  if (km < 0) km = 0;
  d_.odoKm = km;
  saveOdo();
}

void Ride::saveOdo() {
  if (fabs(d_.odoKm - savedOdo_) < 0.0005) return;
  odoSaveKm(d_.odoKm);
  savedOdo_ = d_.odoKm;
  saveTrip();
  LOGF("[ride] odometer saved: %.3f km\n", d_.odoKm);
}

// Adds the energy counted by VESC `idx` since its previous sample to the trip.
void Ride::accumulateEnergy(int idx, bool firstEver) {
  const VescState& s = idx == 0 ? telemetry.primary() : telemetry.secondary();
  Counters& c = c_[idx];
  const float wh = s.v.wh, whc = s.v.whCharged, ah = s.v.ah, ahc = s.v.ahCharged;
  if (!c.init) {
    if (firstEver) {  // display (re)booted mid-ride: continue with what the VESC counted so far
      d_.whUsed += wh;
      d_.whRegen += whc;
      d_.ahUsed += ah;
      d_.ahRegen += ahc;
    }
  } else {
    // A counter that goes backwards means the VESC restarted; it counts from 0 again.
    auto delta = [](float now, float last) { return now >= last ? now - last : now; };
    d_.whUsed += delta(wh, c.wh);
    d_.whRegen += delta(whc, c.whc);
    d_.ahUsed += delta(ah, c.ah);
    d_.ahRegen += delta(ahc, c.ahc);
  }
  c.wh = wh;
  c.whc = whc;
  c.ah = ah;
  c.ahc = ahc;
}

void Ride::maybeSaveOdo(uint32_t now) {
  const double unsaved = d_.odoKm - savedOdo_;
  if (unsaved >= ODO_SAVE_INTERVAL_KM) {
    saveOdo();
    return;
  }
  // Riders nearly always stop before switching off: save once we have stood still for 3 s.
  if (d_.speedKmh < 1.0f) {
    if (!stoppedSince_) stoppedSince_ = now;
    if (now - stoppedSince_ > 3000 && unsaved >= ODO_SAVE_ON_STOP_KM) saveOdo();
  } else {
    stoppedSince_ = 0;
  }
}

void Ride::update(uint32_t now) {
  d_.sessionSec = (now - bootMs_) / 1000;
  d_.connected = telemetry.connected(now);

  const VescState& p = telemetry.primary();
  const VescState& s = telemetry.secondary();
  const bool dual = telemetry.dual();

  if (dual && s.valid && s.seq != lastSeq_[1]) {
    lastSeq_[1] = s.seq;
    accumulateEnergy(1, firstEver_ && !tripRestored_ && !c_[1].init);
    c_[1].init = true;
  }

  if (!p.valid || p.seq == lastSeq_[0]) {
    if (!d_.connected) {
      d_.speedKmh = 0;
      d_.powerW = d_.currentIn = d_.currentMotor = d_.dutyPct = 0;
    }
    maybeSaveOdo(now);
    return;
  }
  lastSeq_[0] = p.seq;

  float dt = lastSampleMs_ ? (now - lastSampleMs_) / 1000.0f : 0.1f;
  if (dt > 2.0f) dt = 2.0f;
  lastSampleMs_ = now;

  const vesc::Values& v = p.v;
  const float poles = cfg.motorPoles;
  const float gear = cfg.gearRatioX100 / 100.0f;
  const float circM = PI * cfg.wheelDiameterMm / 1000.0f * (1.0f + cfg.speedCorrPct / 100.0f);

  // ---- speed ----
  d_.erpm = v.erpm;
  d_.motorRpm = v.erpm / (poles / 2.0f);
  d_.speedKmh = fabsf(d_.motorRpm / gear * circM * 60.0f / 1000.0f);

  // ---- distance (VESC tachometer: 6 counts per electrical revolution) ----
  const float metersPerCount = circM / (poles * 3.0f * gear);
  const int32_t counts = v.hasTachoAbs ? v.tachoAbs : v.tacho;
  Counters& c0 = c_[0];
  if (!c0.init) {
    if (firstEver_ && !tripRestored_ && v.hasTachoAbs) d_.tripKm += counts * metersPerCount / 1000.0f;
  } else {
    int64_t delta = (int64_t)counts - c0.tacho;
    if (!v.hasTachoAbs) delta = delta < 0 ? -delta : delta;
    // plausibility: at most 250 km/h for the elapsed time
    const float maxCounts = (250.0f / 3.6f) * (dt + 0.2f) / metersPerCount;
    if (delta < 0) delta = v.hasTachoAbs ? counts : 0;  // VESC restarted
    if (delta > maxCounts) delta = 0;
    const double km = delta * metersPerCount / 1000.0;
    d_.tripKm += km;
    d_.odoKm += km;
    if (d_.speedKmh > 1.0f) avgDistKm_ += km;
  }
  c0.tacho = counts;

  accumulateEnergy(0, firstEver_ && !tripRestored_ && !c0.init);
  c0.init = true;
  firstEver_ = false;

  // ---- combined electrical values ----
  d_.voltage = v.voltage;
  d_.currentIn = v.currentIn;
  d_.currentMotor = v.currentMotor;
  d_.dutyPct = fabsf(v.duty) * 100.0f;
  d_.tempFet = v.tempFet;
  d_.tempMotor = v.tempMotor;
  d_.fault = v.fault;
  d_.faultVesc = v.fault ? 1 : 0;
  if (dual && s.valid && now - s.updatedMs < LINK_TIMEOUT_MS) {
    d_.currentIn += s.v.currentIn;
    d_.currentMotor += s.v.currentMotor;
    d_.dutyPct = fmaxf(d_.dutyPct, fabsf(s.v.duty) * 100.0f);
    d_.tempFet = fmaxf(d_.tempFet, s.v.tempFet);
    d_.tempMotor = fmaxf(d_.tempMotor, s.v.tempMotor);
    if (!d_.fault && s.v.fault) {
      d_.fault = s.v.fault;
      d_.faultVesc = 2;
    }
  }
  d_.powerW = d_.voltage * d_.currentIn;

  // ---- battery (filtered voltage, tau = 4 s, so load sag does not jump the gauge) ----
  if (vFilt_ <= 0) vFilt_ = d_.voltage;
  vFilt_ += (d_.voltage - vFilt_) * (dt / (4.0f + dt));
  d_.battPct = (d_.voltage > 1.0f && cfg.batteryCells) ? batteryPercentFromCell(vFilt_ / cfg.batteryCells, cfg.battType) : -1;

  // ---- statistics ----
  if (d_.speedKmh > 1.0f) rideMs_ += (uint32_t)(dt * 1000);
  d_.rideSec = rideMs_ / 1000;
  d_.avgSpeedKmh = rideMs_ > 5000 ? (float)(avgDistKm_ / (rideMs_ / 3600000.0)) : 0;
  d_.maxSpeedKmh = fmaxf(d_.maxSpeedKmh, d_.speedKmh);
  d_.maxPowerW = fmaxf(d_.maxPowerW, d_.powerW);
  d_.maxRegenW = fmaxf(d_.maxRegenW, -d_.powerW);
  d_.maxCurrentIn = fmaxf(d_.maxCurrentIn, fabsf(d_.currentIn));
  d_.maxCurrentMotor = fmaxf(d_.maxCurrentMotor, fabsf(d_.currentMotor));
  if (d_.voltage > 1.0f && (d_.minVoltage <= 0 || d_.voltage < d_.minVoltage)) d_.minVoltage = d_.voltage;
  d_.maxTempFet = fmaxf(d_.maxTempFet, d_.tempFet);
  d_.maxTempMotor = fmaxf(d_.maxTempMotor, d_.tempMotor);

  const float netWh = d_.whUsed - d_.whRegen;
  d_.whPerKm = (d_.tripKm >= 0.2f && netWh > 0) ? netWh / d_.tripKm : -1;
  d_.rangeKm = -1;
  if (d_.whPerKm > 0.5f && d_.battPct >= 0 && cfg.batteryAhX10 > 0) {
    const float remainingWh = cfg.batteryCells * 3.6f * (cfg.batteryAhX10 / 10.0f) * d_.battPct / 100.0f;
    d_.rangeKm = remainingWh / d_.whPerKm;
  }

  d_.seq++;
  maybeSaveOdo(now);
}
