#include "alerts.h"
#include "ride.h"
#include "settings.h"
#include "../vesc/telemetry.h"
#include "../vesc/vesc_protocol.h"
#include "../hal/status_led.h"
#include "../hal/buzzer.h"

static AlertState s_state;
static uint16_t   s_prevActive = 0;
static uint32_t   s_lastCritBeep = 0;
static uint32_t   s_bootMs = 0;

const AlertState& alerts() { return s_state; }

void alertsBegin() {
  s_bootMs = millis();
  statusLedBegin();
  buzzerBegin();
}

void alertsUpdate(uint32_t now) {
  const RideData& r = ride.data();
  uint16_t a = 0;

  if (!r.connected) {
    // give the link a few seconds after boot before complaining
    if (now - s_bootMs > 4000) a |= AL_NO_LINK;
  } else {
    if (r.fault) a |= AL_FAULT;
    if (r.tempFet >= cfg.fetTempCrit) a |= AL_FET_CRIT;
    else if (r.tempFet >= cfg.fetTempWarn) a |= AL_FET_WARN;
    if (r.tempMotor >= cfg.motorTempCrit) a |= AL_MOT_CRIT;
    else if (r.tempMotor >= cfg.motorTempWarn) a |= AL_MOT_WARN;

    const float cells = cfg.batteryCells;
    if (r.voltage > 1.0f) {
      if (r.voltage < cells * cellEmptyVoltage(cfg.battType)) a |= AL_BATT_EMPTY;
      else if (r.battPct >= 0 && r.battPct <= cfg.warnBattPct) a |= AL_BATT_LOW;
      if (r.voltage > cells * cellMaxVoltage(cfg.battType)) a |= AL_BATT_HIGH;
    }
    if (r.speedKmh > cfg.warnSpeed) a |= AL_SPEED;
    if (r.dutyPct >= cfg.warnDutyPct) a |= AL_DUTY;
  }

  // Highest priority message first
  const char* msg = "";
  static char buf[40];
  if (a & AL_FAULT) {
    snprintf(buf, sizeof(buf), "FAULT: %s", vesc::faultName(r.fault));
    msg = buf;
  } else if (a & AL_FET_CRIT)   msg = "ESC OVERHEAT";
  else if (a & AL_MOT_CRIT)     msg = "MOTOR OVERHEAT";
  else if (a & AL_BATT_EMPTY)   msg = "BATTERY EMPTY";
  else if (a & AL_BATT_HIGH)    msg = "BATTERY OVERVOLTAGE";
  else if (a & AL_DUTY)         msg = "HIGH DUTY - SLOW DOWN";
  else if (a & AL_NO_LINK)      msg = "NO VESC LINK";
  else if (a & AL_BATT_LOW)     msg = "BATTERY LOW";
  else if (a & AL_FET_WARN)     msg = "ESC HOT";
  else if (a & AL_MOT_WARN)     msg = "MOTOR HOT";
  else if (a & AL_SPEED)        msg = "SPEED WARNING";
  strncpy(s_state.text, msg, sizeof(s_state.text) - 1);

  const uint16_t critMask = AL_FAULT | AL_FET_CRIT | AL_MOT_CRIT | AL_BATT_EMPTY | AL_BATT_HIGH | AL_DUTY;
  const uint16_t warnMask = AL_FET_WARN | AL_MOT_WARN | AL_BATT_LOW | AL_SPEED;
  s_state.level = (a & critMask) ? AlertLevel::CRIT : (a & warnMask) ? AlertLevel::WARN : AlertLevel::OK;
  s_state.active = a;

  // ---- LED ----
  LedColor color;
  bool blink = false;
  if (cfg.demoMode)                        color = LED_PURPLE;
  else if (!r.connected)                   { color = LED_BLUE; blink = true; }
  else if (s_state.level == AlertLevel::CRIT) { color = LED_RED; blink = true; }
  else if (s_state.level == AlertLevel::WARN) color = LED_YELLOW;
  else                                     color = LED_GREEN;
  statusLedSet(cfg.led ? color : LED_OFF, blink);
  statusLedUpdate(now);

  // ---- sound: only on new alerts, critical ones repeat every 15 s ----
  const uint16_t newBits = a & ~s_prevActive;
  static uint32_t lastDutyBeep = 0;
  if (cfg.buzzer) {
    if ((a & AL_DUTY) && now - lastDutyBeep > 1200) {  // keeps beeping while duty is too high
      buzzerPlay(BEEP_DUTY);
      lastDutyBeep = now;
    }
    else if (newBits & critMask)     { buzzerPlay(BEEP_CRITICAL); s_lastCritBeep = now; }
    else if (newBits & (warnMask & ~AL_SPEED)) buzzerPlay(BEEP_WARNING);
    else if ((newBits & AL_SPEED) && cfg.speedBeep) buzzerPlay(BEEP_SHORT);
    else if ((a & critMask & ~AL_DUTY) && now - s_lastCritBeep > 15000) {
      buzzerPlay(BEEP_CRITICAL);
      s_lastCritBeep = now;
    }
  }
  buzzerUpdate(now);
  s_prevActive = a;
}
