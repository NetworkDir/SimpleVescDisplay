#pragma once
#include <Arduino.h>

enum class AlertLevel : uint8_t { OK = 0, WARN = 1, CRIT = 2 };

enum AlertBits : uint16_t {
  AL_NO_LINK    = 1 << 0,
  AL_FAULT      = 1 << 1,
  AL_FET_WARN   = 1 << 2,
  AL_FET_CRIT   = 1 << 3,
  AL_MOT_WARN   = 1 << 4,
  AL_MOT_CRIT   = 1 << 5,
  AL_BATT_LOW   = 1 << 6,
  AL_BATT_EMPTY = 1 << 7,
  AL_BATT_HIGH  = 1 << 8,
  AL_SPEED      = 1 << 9,
  AL_DUTY       = 1 << 10,
};

struct AlertState {
  uint16_t   active = 0;       // AlertBits
  AlertLevel level = AlertLevel::OK;
  char       text[40] = {0};   // most important active alert, "" if none
};

// Evaluates warnings, drives the RGB LED and the speaker.
void alertsBegin();
void alertsUpdate(uint32_t nowMs);
const AlertState& alerts();
