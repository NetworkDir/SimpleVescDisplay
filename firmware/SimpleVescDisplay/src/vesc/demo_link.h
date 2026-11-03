#pragma once
#include <Arduino.h>
#include "vesc_link.h"

// Simulated VESC(s) for bench testing without a controller.
// It answers real protocol requests with real COMM_GET_VALUES payloads, so the
// whole parsing / telemetry / ride pipeline is exercised exactly like on a scooter.
class DemoLink : public VescLink {
 public:
  bool begin() override;
  void end() override {}
  void poll(uint32_t nowMs) override;
  bool send(uint8_t targetId, const uint8_t* payload, size_t len) override;
  const char* name() const override { return "DEMO"; }

 private:
  void simulate(uint32_t now);
  size_t buildValues(uint8_t* out, bool secondary);
  float targetSpeed(float t) const;

  uint32_t lastSim_ = 0;
  uint32_t start_ = 0;
  float speed_ = 0;       // km/h
  float currentIn_ = 0;   // A
  float voltage_ = 0;
  float tempFet_ = 28, tempMotor_ = 26;
  float ah_ = 0, ahCh_ = 0, wh_ = 0, whCh_ = 0;
  double tachoAbs_ = 0;
  float erpm_ = 0;
  float duty_ = 0;
  vesc::McconfTemp limits_;   // simulated temporary motor limits (ride modes)

  uint8_t reply_[96];
  size_t replyLen_ = 0;
  uint8_t replySrc_ = VESC_TARGET_LOCAL;
  uint32_t replyAt_ = 0;
};
