#include "demo_link.h"
#include <math.h>
#include <string.h>
#include "../core/settings.h"

using namespace vesc;

bool DemoLink::begin() {
  start_ = lastSim_ = millis();
  voltage_ = cfg.batteryCells * 4.05f;
  limits_.currentMinScale = limits_.currentMaxScale = 1.0f;
  limits_.minErpm = -100000;
  limits_.maxErpm = 100000;
  limits_.minDuty = 0.005f;
  limits_.maxDuty = 0.95f;
  limits_.wattMin = -1500000;
  limits_.wattMax = 1500000;
  limits_.inCurrentMin = -30;
  limits_.inCurrentMax = 60;
  limits_.hasInCurrent = true;
  return true;
}

// A repeating 70 s "ride": start, cruise, a hard pull past the speed warning, braking.
float DemoLink::targetSpeed(float t) const {
  t = fmodf(t, 70.0f);
  if (t < 4) return 0;
  if (t < 16) return 52;
  if (t < 40) return 46 + 7 * sinf(t * 0.5f);
  if (t < 46) return 68;
  if (t < 56) return 38;
  if (t < 64) return 0;
  return 0;
}

void DemoLink::simulate(uint32_t now) {
  float dt = (now - lastSim_) / 1000.0f;
  if (dt <= 0) return;
  if (dt > 0.5f) dt = 0.5f;
  lastSim_ = now;

  const float t = (now - start_) / 1000.0f;
  const float circ0 = PI * cfg.wheelDiameterMm / 1000.0f;
  const float erpmPerKmh = 1.0f / 3.6f / circ0 * 60.0f * (cfg.gearRatioX100 / 100.0f) * (cfg.motorPoles / 2.0f);
  const float vmax = limits_.maxErpm / erpmPerKmh;     // ride mode speed limit
  float target = targetSpeed(t);
  if (target > vmax) target = vmax;
  const float prev = speed_;
  const float maxAcc = target > speed_ ? 9.0f * limits_.currentMaxScale : 14.0f;  // km/h per second
  float diff = target - speed_;
  float step = maxAcc * dt;
  speed_ += diff > step ? step : (diff < -step ? -step : diff);
  if (speed_ < 0) speed_ = 0;

  // simple vehicle model: 90 kg, rolling + aero drag
  const float v = speed_ / 3.6f;
  const float acc = (speed_ - prev) / 3.6f / dt;
  float pMech = (90.0f * acc + 18.0f + 0.32f * v * v) * v;
  float pBatt = pMech >= 0 ? pMech / 0.85f : pMech * 0.65f;
  if (speed_ < 0.5f) pBatt = 4.0f;  // controller idle consumption

  const float capWh = cfg.batteryCells * 3.6f * (cfg.batteryAhX10 / 10.0f > 1 ? cfg.batteryAhX10 / 10.0f : 20.0f);
  float soc = 0.9f - (wh_ - whCh_) / capWh;
  if (soc < 0) soc = 0;
  const float ocv = cfg.batteryCells * (3.45f + 0.72f * soc);
  currentIn_ = pBatt / (voltage_ > 10 ? voltage_ : ocv);
  voltage_ = ocv - currentIn_ * 0.09f;

  const float circ = PI * cfg.wheelDiameterMm / 1000.0f;
  erpm_ = speed_ / 3.6f / circ * 60.0f * (cfg.gearRatioX100 / 100.0f) * (cfg.motorPoles / 2.0f);
  duty_ = speed_ / 80.0f;  // 80 km/h at 100 % duty
  if (duty_ > 0.95f) duty_ = 0.95f;

  tachoAbs_ += fabsf(erpm_) * dt / 10.0f;  // 6 counts per electrical revolution
  if (currentIn_ >= 0) {
    ah_ += currentIn_ * dt / 3600.0f;
    wh_ += pBatt * dt / 3600.0f;
  } else {
    ahCh_ += -currentIn_ * dt / 3600.0f;
    whCh_ += -pBatt * dt / 3600.0f;
  }

  // first order thermal models
  tempFet_ += ((28 + fabsf(currentIn_) * 0.9f) - tempFet_) * dt / 40.0f;
  tempMotor_ += ((26 + fabsf(currentIn_) * 1.4f) - tempMotor_) * dt / 90.0f;
}

size_t DemoLink::buildValues(uint8_t* p, bool secondary) {
  const float k = secondary ? 0.94f : 1.0f;
  const float motorCurrent = duty_ > 0.05f ? currentIn_ / duty_ : currentIn_;
  int32_t i = 0;
  p[i++] = COMM_GET_VALUES;
  putI16(p, (int16_t)((tempFet_ + (secondary ? 2 : 0)) * 10), i);
  putI16(p, (int16_t)((tempMotor_ + (secondary ? 3 : 0)) * 10), i);
  putI32(p, (int32_t)(motorCurrent * k * 100), i);
  putI32(p, (int32_t)(currentIn_ * k * 100), i);
  putI32(p, 0, i);  // id
  putI32(p, 0, i);  // iq
  putI16(p, (int16_t)(duty_ * 1000), i);
  putI32(p, (int32_t)erpm_, i);
  putI16(p, (int16_t)(voltage_ * 10), i);
  putI32(p, (int32_t)(ah_ * k * 10000), i);
  putI32(p, (int32_t)(ahCh_ * k * 10000), i);
  putI32(p, (int32_t)(wh_ * k * 10000), i);
  putI32(p, (int32_t)(whCh_ * k * 10000), i);
  putI32(p, (int32_t)tachoAbs_, i);
  putI32(p, (int32_t)tachoAbs_, i);
  p[i++] = 0;       // fault
  putI32(p, 0, i);  // pid pos
  p[i++] = secondary ? cfg.secondVescId : 1;
  return (size_t)i;
}

bool DemoLink::send(uint8_t target, const uint8_t* payload, size_t len) {
  if (len == 0) return false;
  const bool secondary = (target != VESC_TARGET_LOCAL && target == cfg.secondVescId);
  switch (payload[0]) {
    case COMM_GET_VALUES:
      simulate(millis());
      replyLen_ = buildValues(reply_, secondary);
      break;
    case COMM_FW_VERSION: {
      static const char hw[] = "DEMO";
      reply_[0] = COMM_FW_VERSION;
      reply_[1] = 6;
      reply_[2] = 5;
      memcpy(reply_ + 3, hw, sizeof(hw));
      replyLen_ = 3 + sizeof(hw);
      break;
    }
    case COMM_PING_CAN:
      reply_[0] = COMM_PING_CAN;
      reply_[1] = 2;
      replyLen_ = 2;
      break;
    case COMM_GET_MCCONF_TEMP: {
      int32_t i = 0;
      reply_[i++] = COMM_GET_MCCONF_TEMP;
      putF32Auto(reply_, limits_.currentMinScale, i);
      putF32Auto(reply_, limits_.currentMaxScale, i);
      putF32Auto(reply_, limits_.minErpm, i);
      putF32Auto(reply_, limits_.maxErpm, i);
      putF32Auto(reply_, limits_.minDuty, i);
      putF32Auto(reply_, limits_.maxDuty, i);
      putF32Auto(reply_, limits_.wattMin, i);
      putF32Auto(reply_, limits_.wattMax, i);
      putF32Auto(reply_, limits_.inCurrentMin, i);
      putF32Auto(reply_, limits_.inCurrentMax, i);
      reply_[i++] = cfg.motorPoles;
      putF32Auto(reply_, cfg.gearRatioX100 / 100.0f, i);
      putF32Auto(reply_, cfg.wheelDiameterMm / 1000.0f, i);
      replyLen_ = (size_t)i;
      break;
    }
    case COMM_SET_MCCONF_TEMP: {
      if (len < 37) return true;
      int32_t i = 5;  // skip id + 4 flag bytes
      limits_.currentMinScale = getF32Auto(payload, i);
      limits_.currentMaxScale = getF32Auto(payload, i);
      limits_.minErpm = getF32Auto(payload, i);
      limits_.maxErpm = getF32Auto(payload, i);
      reply_[0] = COMM_SET_MCCONF_TEMP;  // ack
      replyLen_ = 1;
      break;
    }
    default:
      return true;
  }
  replySrc_ = target;
  replyAt_ = millis() + 8;  // pretend transfer time
  return true;
}

void DemoLink::poll(uint32_t now) {
  if (replyLen_ && (int32_t)(now - replyAt_) >= 0) {
    size_t n = replyLen_;
    replyLen_ = 0;
    dispatch(replySrc_, reply_, n);
  }
}
