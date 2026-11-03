#include "ride_modes.h"
#include <Preferences.h>
#include <math.h>
#include "settings.h"
#include "ride.h"
#include "../vesc/telemetry.h"
#include "../config/app_config.h"

static const char* const kNames[MODE_COUNT] = {"ECO", "DRIVE", "SPORT", "SPORT+"};
static const uint32_t kColors[MODE_COUNT] = {0x2EE66B, 0x00B4FF, 0xFF8A1E, 0xFF2D55};

static ModeState        s_state = ModeState::OFF;
static bool             s_applied = false;     // limits of s_appliedMode are active in the VESC
static uint8_t          s_appliedMode = 0xFF;
static bool             s_baseValid = false;
static vesc::McconfTemp s_base;                // the VESC's own limits
static vesc::McconfTemp s_sent;                // limits we applied last
static uint32_t         s_seq = 0, s_failSeq = 0, s_stepAt = 0, s_lastVerify = 0;
static uint8_t          s_retries = 0;
static uint8_t          s_tcsSent = 0xFF;

const char* rideModeName(uint8_t m) { return m < MODE_COUNT ? kNames[m] : "?"; }
uint32_t rideModeColor(uint8_t m) { return m < MODE_COUNT ? kColors[m] : 0x00B4FF; }
uint8_t rideModeActive() { return cfg.rideMode; }
ModeState rideModeState() { return s_state; }

bool rideModeTcsSupported() {
  const vesc::FwInfo& fw = telemetry.fw();
  return fw.valid && fw.major >= 7;  // 'adc-tc exists from FW 7.00.2
}

// ---- remembering the VESC's own limits across display restarts -----------------
// If the display restarts while the VESC still runs our temporary limits, reading
// them back would give *our* values. So the original values and the values we
// applied are kept in flash; when the VESC reports exactly what we applied, the
// stored originals are used. After a VESC restart it reports its own values again.
struct BaseStore {
  float maxErpm, minScale, maxScale;     // the VESC's own
  float aMaxErpm, aMinScale, aMaxScale;  // what we applied
};

static bool near(float a, float b) { return fabsf(a - b) <= fmaxf(0.005f, fabsf(b) * 0.01f); }

static void saveStore() {
  BaseStore st = {s_base.maxErpm, s_base.currentMinScale, s_base.currentMaxScale,
                  s_sent.maxErpm, s_sent.currentMinScale, s_sent.currentMaxScale};
  Preferences p;
  if (p.begin("svd", false)) {
    p.putBytes("rmbase", &st, sizeof(st));
    p.end();
  }
}

static void takeBaseline(const vesc::McconfTemp& r) {
  s_base = r;
  BaseStore st;
  Preferences p;
  bool have = false;
  if (p.begin("svd", true)) {
    have = p.getBytesLength("rmbase") == sizeof(st) && p.getBytes("rmbase", &st, sizeof(st)) == sizeof(st);
    p.end();
  }
  if (have && near(r.maxErpm, st.aMaxErpm) && near(r.currentMinScale, st.aMinScale) &&
      near(r.currentMaxScale, st.aMaxScale)) {
    s_base.maxErpm = st.maxErpm;
    s_base.currentMinScale = st.minScale;
    s_base.currentMaxScale = st.maxScale;
  }
  s_baseValid = true;
  LOGF("[mode] VESC limits: scale %.2f/%.2f erpm %.0f..%.0f duty %.3f watt %.0f\n", s_base.currentMinScale,
       s_base.currentMaxScale, s_base.minErpm, s_base.maxErpm, s_base.maxDuty, s_base.wattMax);
}

static float kmhToErpm(float kmh) {
  const float circ = PI * cfg.wheelDiameterMm / 1000.0f * (1.0f + cfg.speedCorrPct / 100.0f);
  return kmh / 3.6f / circ * 60.0f * (cfg.gearRatioX100 / 100.0f) * (cfg.motorPoles / 2.0f);
}

static vesc::McconfTemp limitsFor(uint8_t mode) {
  vesc::McconfTemp m = s_base;  // duty, power, battery current and reverse ERPM stay untouched
  m.currentMaxScale = s_base.currentMaxScale * cfg.modeCurrent[mode] / 100.0f;
  m.currentMinScale = s_base.currentMinScale * cfg.modeBrake[mode] / 100.0f;
  if (cfg.modeSpeed[mode]) {
    const float cap = kmhToErpm(cfg.modeSpeed[mode]);
    if (cap > 0 && cap < m.maxErpm) m.maxErpm = cap;  // never above the VESC's own limit
  }
  return m;
}

// ---- state machine -------------------------------------------------------------
static void startRead(uint32_t now) {
  if (!telemetry.queueReadMcconfTemp()) return;
  s_seq = telemetry.mcconfSeq();
  s_failSeq = telemetry.jobFailSeq();
  s_stepAt = now;
  s_state = ModeState::READING;
}

static void startApply(uint32_t now) {
  s_sent = limitsFor(cfg.rideMode);
  if (!telemetry.queueWriteMcconfTemp(s_sent)) return;  // busy: try again next loop
  s_seq = telemetry.setAckSeq();
  s_failSeq = telemetry.jobFailSeq();
  s_stepAt = now;
  s_state = ModeState::APPLYING;
}

static void failed(uint32_t now) {
  if (++s_retries >= 3) {
    s_retries = 0;
    s_state = ModeState::FAILED;
    LOGF("[mode] VESC does not answer the ride mode commands\n");
  } else {
    s_state = ModeState::WAITING;
  }
  s_stepAt = now;
}

void rideModesUpdate(uint32_t now) {
  const bool conn = telemetry.connected(now);

  if (!cfg.rideModes) {
    // switched off: give the VESC its own limits back right away
    if (s_applied && s_baseValid && conn && !telemetry.jobBusy()) {
      s_sent = s_base;
      if (telemetry.queueWriteMcconfTemp(s_base)) {
        s_applied = false;
        saveStore();
        LOGF("[mode] ride modes off, VESC limits restored\n");
      }
    }
    if (!s_applied) s_state = ModeState::OFF;
    return;
  }

  if (!conn) {  // the VESC may restart meanwhile: read its limits again on reconnect
    s_state = ModeState::WAITING;
    s_applied = false;
    s_baseValid = false;
    s_tcsSent = 0xFF;
    return;
  }

  switch (s_state) {
    case ModeState::FAILED:
      if (now - s_stepAt < 15000) break;  // retry from time to time
      // fall through
    case ModeState::OFF:
    case ModeState::WAITING:
      startRead(now);
      break;

    case ModeState::READING:
      if (telemetry.mcconfSeq() != s_seq) {
        const vesc::McconfTemp& r = telemetry.mcconf();
        s_retries = 0;
        if (!s_applied || !s_baseValid) {
          takeBaseline(r);
          startApply(now);
        } else if (!near(r.currentMaxScale, s_sent.currentMaxScale) || !near(r.maxErpm, s_sent.maxErpm)) {
          LOGF("[mode] limits changed in the VESC (restart?), applying %s again\n", rideModeName(cfg.rideMode));
          s_applied = false;
          takeBaseline(r);
          startApply(now);
        } else {
          s_state = ModeState::ACTIVE;
        }
      } else if (telemetry.jobFailSeq() != s_failSeq || now - s_stepAt > 1500) {
        failed(now);
      }
      break;

    case ModeState::APPLYING:
      if (telemetry.setAckSeq() != s_seq) {
        s_applied = true;
        s_appliedMode = cfg.rideMode;
        s_retries = 0;
        s_state = ModeState::ACTIVE;
        s_lastVerify = now;
        saveStore();
        LOGF("[mode] %s active: current %.0f%% regen %.0f%% max erpm %.0f\n", rideModeName(s_appliedMode),
             s_sent.currentMaxScale * 100, s_sent.currentMinScale * 100, s_sent.maxErpm);
      } else if (telemetry.jobFailSeq() != s_failSeq || now - s_stepAt > 1500) {
        failed(now);
      }
      break;

    case ModeState::ACTIVE:
      if (s_appliedMode != cfg.rideMode) {
        // new mode or edited mode settings - never change the limits while riding
        if (ride.data().speedKmh <= 3.0f) startApply(now);
      } else if (cfg.tcsControl && rideModeTcsSupported() && s_tcsSent != cfg.modeTcs[cfg.rideMode]) {
        const uint8_t want = cfg.modeTcs[cfg.rideMode];
        if (telemetry.queueLisp(want ? "(conf-set 'adc-tc 1)" : "(conf-set 'adc-tc 0)")) {
          s_tcsSent = want;
          LOGF("[mode] traction control %s\n", want ? "on" : "off");
        }
      } else if (now - s_lastVerify > 5000) {
        s_lastVerify = now;
        startRead(now);  // verify the limits are still active
      }
      break;
  }
}

void rideModesParamsChanged() {
  if (s_state == ModeState::ACTIVE) s_appliedMode = 0xFF;
}

const char* rideModeStateText() {
  switch (s_state) {
    case ModeState::OFF:      return "off";
    case ModeState::WAITING:  return "waiting for the VESC";
    case ModeState::READING:  return "reading VESC limits";
    case ModeState::APPLYING: return "applying";
    case ModeState::ACTIVE:   return "active";
    default:                  return "VESC did not answer";
  }
}

bool rideModeSelect(uint8_t mode, const char** whyNot) {
  static const char* reason = "";
  if (whyNot) *whyNot = reason;
  if (mode >= MODE_COUNT) return false;
  if (!cfg.rideModes) {
    reason = "Ride modes are off (Settings > Ride modes)";
    if (whyNot) *whyNot = reason;
    return false;
  }
  if (ride.data().speedKmh > 3.0f) {
    reason = "Stop to change the ride mode";
    if (whyNot) *whyNot = reason;
    return false;
  }
  cfg.rideMode = mode;
  settingsMarkDirty();
  return true;
}
