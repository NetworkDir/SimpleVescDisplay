#include "telemetry.h"
#include "uart_link.h"
#include "can_link.h"
#include "demo_link.h"
#include "../ble/ble_link.h"
#include "../ble/ble_bridge.h"
#include "../core/settings.h"
#include "../config/app_config.h"
#include "../config/board_config.h"

using namespace vesc;

Telemetry telemetry;

static HardwareSerial s_vescSerial(2);

static constexpr uint32_t PING_CAN_TIMEOUT_MS = 6000;
static constexpr uint32_t TOOL_REPLY_WINDOW_MS = 1500;

void Telemetry::begin() {
  if (cfg.demoMode) {
    link_ = new DemoLink();
  } else if (cfg.commMode == COMM_CAN) {
    can_ = new CanLink(PIN_VESC_RX, PIN_VESC_TX, cfg.canBitrate, cfg.canOwnId);
    link_ = can_;
  } else if (cfg.commMode == COMM_BLE) {
    link_ = new BleLink();
  } else {
    link_ = new UartLink(s_vescSerial, cfg.uartBaud, PIN_VESC_RX, PIN_VESC_TX);
  }
  link_->setHandler(onPacketThunk, this);
  bool ok = link_->begin();
  LOGF("[vesc] %s link %s (rx=%d tx=%d)\n", link_->name(), ok ? "started" : "FAILED", PIN_VESC_RX, PIN_VESC_TX);

  // VESC Tool bridge: only with a real wired link to forward to
  if (cfg.bleBridge && !cfg.demoMode && cfg.commMode != COMM_BLE) bridge_ = bleBridge.begin();
  settingsChanged();
}

void Telemetry::settingsChanged() {
  if (can_) primaryCanId_ = (cfg.canVescId != 255) ? cfg.canVescId : -1;
  if (!dual()) vesc_[1] = VescState();
}

bool Telemetry::dual() const {
  return cfg.secondVescId != 255 && (int)cfg.secondVescId != primaryCanId_;
}

bool Telemetry::connected(uint32_t now) const {
  return vesc_[0].valid && (now - vesc_[0].updatedMs) < LINK_TIMEOUT_MS;
}

bool Telemetry::canBusOff() const { return can_ && can_->busOff(); }

uint32_t Telemetry::errors() const {
  return parseErrors_ + (link_ ? link_->errorCount() : 0);
}

int Telemetry::primaryTarget() const {
  if (can_) return primaryCanId_;
  return VESC_TARGET_LOCAL;
}

int Telemetry::secondaryTarget() const {
  return dual() ? cfg.secondVescId : -1;
}

void Telemetry::onPacketThunk(void* ctx, uint8_t src, const uint8_t* p, size_t len) {
  static_cast<Telemetry*>(ctx)->onPacket(src, p, len);
}

void Telemetry::apply(int idx, const Values& v, DataSource src, uint32_t now) {
  VescState& s = vesc_[idx];
  s.v = v;
  s.valid = true;
  s.updatedMs = now;
  s.seq++;
  s.source = src;
}

void Telemetry::noteToolRequest(const uint8_t* p, size_t len, uint32_t now) {
  // the reply of a forwarded command carries the inner command id
  const uint8_t cmd = (p[0] == COMM_FORWARD_CAN && len >= 3) ? p[2] : p[0];
  toolReq_[toolReqNext_] = {cmd, now | 1u};  // at == 0 marks a free slot
  toolReqNext_ = (toolReqNext_ + 1) % (sizeof(toolReq_) / sizeof(toolReq_[0]));
}

bool Telemetry::toolAwaits(uint8_t cmd, uint32_t now) {
  for (auto& r : toolReq_) {
    if (r.at && r.cmd == cmd && now - r.at < TOOL_REPLY_WINDOW_MS) {
      r.at = 0;
      return true;
    }
  }
  return false;
}

void Telemetry::onPacket(uint8_t src, const uint8_t* p, size_t len) {
  const uint32_t now = millis();
  rxPackets_++;

  // Replies to our own requests stay here, everything else goes to VESC Tool -
  // unless VESC Tool asked the same thing (e.g. both poll COMM_GET_VALUES).
  bool ours = (p[0] == COMM_GET_VALUES && pending_ == REQ_VALUES) ||
              (p[0] == COMM_FW_VERSION && pending_ == REQ_FW) ||
              (p[0] == COMM_PING_CAN && pending_ == REQ_PING_CAN) ||
              (pending_ == REQ_JOB && p[0] == jobReply_);
  if (bridge_ && (!ours || toolAwaits(p[0], now))) bleBridge.sendPacket(p, len);

  if (pending_ == REQ_JOB && p[0] == jobReply_) pending_ = REQ_NONE;

  switch (p[0]) {
    case COMM_GET_MCCONF_TEMP:
      if (ours && parseMcconfTemp(p, len, mcconf_)) mcconfSeq_++;
      break;
    case COMM_SET_MCCONF_TEMP:
      if (ours) setAckSeq_++;
      break;
    case COMM_GET_VALUES: {
      Values v;
      if (!parseValues(p, len, v)) {
        parseErrors_++;
        break;
      }
      int idx = 0;
      if (dual()) {
        if (can_) {
          idx = (src == cfg.secondVescId) ? 1 : 0;
        } else if (len >= 59) {
          // the reply carries the controller id of the VESC that answered
          idx = (v.controllerId == cfg.secondVescId) ? 1 : 0;
        } else if (pending_ == REQ_VALUES) {
          idx = pendingIdx_;
        }
      }
      apply(idx, v, DataSource::POLL, now);
      lastPoll_[idx] = now;
      if (pending_ == REQ_VALUES) {
        latency_ = (uint16_t)(now - sentAt_);
        pending_ = REQ_NONE;
      }
      break;
    }
    case COMM_FW_VERSION:
      if (parseFwVersion(p, len, fw_)) {
        LOGF("[vesc] FW %u.%02u HW '%s'\n", fw_.major, fw_.minor, fw_.hw);
      }
      if (pending_ == REQ_FW) pending_ = REQ_NONE;
      break;
    case COMM_PING_CAN:
      scanCount_ = 0;
      for (size_t i = 1; i < len && scanCount_ < sizeof(scanIds_); i++) scanIds_[scanCount_++] = p[i];
      scanState_ = ScanState::DONE;
      if (pending_ == REQ_PING_CAN) pending_ = REQ_NONE;
      break;
    default:
      break;  // unsolicited packets (e.g. COMM_PRINT) are ignored
  }
}

bool Telemetry::request(Req kind, uint8_t cmd, uint8_t target, uint32_t timeoutMs, int idx) {
  uint8_t payload[1] = {cmd};
  const uint32_t now = millis();
  if (!link_->send(target, payload, 1)) {
    // CAN TX queue full / bus down: try again on the next cycle
    return false;
  }
  pending_ = kind;
  pendingIdx_ = idx;
  sentAt_ = now;
  deadline_ = now + timeoutMs;
  return true;
}

bool Telemetry::queueJob(const uint8_t* payload, size_t len, uint8_t replyId) {
  if (!link_ || jobBusy() || len > sizeof(jobBuf_)) return false;
  memcpy(jobBuf_, payload, len);
  jobLen_ = len;
  jobReply_ = replyId;
  jobQueued_ = true;
  return true;
}

bool Telemetry::queueReadMcconfTemp() {
  const uint8_t cmd = COMM_GET_MCCONF_TEMP;
  return queueJob(&cmd, 1, COMM_GET_MCCONF_TEMP);
}

bool Telemetry::queueWriteMcconfTemp(const McconfTemp& m) {
  uint8_t buf[64];
  const size_t n = buildSetMcconfTemp(m, true, buf);
  return queueJob(buf, n, COMM_SET_MCCONF_TEMP);
}

bool Telemetry::queueLisp(const char* expr) {
  uint8_t buf[64];
  const size_t n = strlen(expr);
  if (n + 2 > sizeof(buf)) return false;
  buf[0] = COMM_LISP_REPL_CMD;
  memcpy(buf + 1, expr, n + 1);  // including the terminating 0
  return queueJob(buf, n + 2, 0xFF);
}

void Telemetry::resolveCanTarget(uint32_t now) {
  if (cfg.canVescId != 255) {
    primaryCanId_ = cfg.canVescId;
    return;
  }
  // Auto mode: forget the id if the node went silent for a while
  if (primaryCanId_ >= 0) {
    if (!vesc_[0].valid || now - vesc_[0].updatedMs < 5000) return;
    primaryCanId_ = -1;
  }
  const uint8_t exclude = cfg.secondVescId;
  int id = can_->firstStatusNode(now, exclude);
  if (id >= 0) {
    primaryCanId_ = id;
    LOGF("[can] auto: VESC %d found from status messages\n", id);
    return;
  }
  if (autoScanActive_) {
    if (!can_->scanning()) {
      autoScanActive_ = false;
      for (uint8_t i = 0; i < can_->foundCount(); i++) {
        uint8_t fid = can_->foundIds()[i];
        if (fid == exclude) continue;
        if (primaryCanId_ < 0 || fid < primaryCanId_) primaryCanId_ = fid;
      }
      if (primaryCanId_ >= 0) LOGF("[can] auto: VESC %d found by ping\n", primaryCanId_);
    }
  } else if (now - lastAutoScan_ > 8000 && scanState_ != ScanState::RUNNING) {
    lastAutoScan_ = now;
    autoScanActive_ = true;
    can_->startPingScan();
  }
}

void Telemetry::startScan() {
  if (!link_ || scanState_ == ScanState::RUNNING) return;
  scanCount_ = 0;
  scanState_ = ScanState::RUNNING;
  if (can_) {
    autoScanActive_ = false;
    can_->startPingScan();
  } else {
    pending_ = REQ_NONE;  // the scan takes priority over a pending poll
    if (!request(REQ_PING_CAN, COMM_PING_CAN, VESC_TARGET_LOCAL, PING_CAN_TIMEOUT_MS, 0)) {
      scanState_ = ScanState::DONE;
    }
  }
}

void Telemetry::update(uint32_t now) {
  if (!link_) return;
  link_->poll(now);

  if (bridge_) {
    // VESC Tool -> VESC (a few packets per loop keeps the UI responsive)
    const uint8_t* p;
    size_t n;
    for (int i = 0; i < 4 && bleBridge.readPacket(p, n); i++) {
      int t = primaryTarget();
      if (t >= 0 && link_->send((uint8_t)t, p, n)) noteToolRequest(p, n, now);
    }
    bleBridge.service(now);
  }

  if (can_) {
    resolveCanTarget(now);

    if (scanState_ == ScanState::RUNNING && !can_->scanning()) {
      scanCount_ = can_->foundCount();
      memcpy(scanIds_, can_->foundIds(), scanCount_);
      scanState_ = ScanState::DONE;
    }

    // Fallback: use CAN status broadcasts when polling gets no answer
    for (int idx = 0; idx < 2; idx++) {
      int id = idx == 0 ? primaryCanId_ : secondaryTarget();
      if (id < 0) continue;
      if (now - lastPoll_[idx] < 600 || now - lastStatusApply_[idx] < 100) continue;
      Values v;
      if (can_->statusFor((uint8_t)id, v, now)) {
        lastStatusApply_[idx] = now;
        apply(idx, v, DataSource::CAN_STATUS, now);
      }
    }
  }

  // Request timeout
  if (pending_ != REQ_NONE && (int32_t)(now - deadline_) >= 0) {
    if (pending_ == REQ_PING_CAN) {
      scanState_ = ScanState::DONE;
    } else if (pending_ == REQ_JOB) {
      jobFailSeq_++;
    } else {
      timeouts_++;
    }
    pending_ = REQ_NONE;
  }

  if (pending_ != REQ_NONE || scanState_ == ScanState::RUNNING) return;

  const uint32_t replyMs = link_->replyTimeoutMs();

  // queued ride-mode job goes first
  if (jobQueued_) {
    const int t = primaryTarget();
    if (t >= 0 && link_->send((uint8_t)t, jobBuf_, jobLen_)) {
      jobQueued_ = false;
      if (jobReply_ != 0xFF) {
        pending_ = REQ_JOB;
        sentAt_ = now;
        deadline_ = now + 400 + replyMs;
      }
      return;
    }
  }

  const uint32_t period = 1000u / (cfg.pollHz ? cfg.pollHz : 10);
  if ((int32_t)(now - nextPoll_) >= 0) {
    nextPoll_ += period;
    if ((int32_t)(now - nextPoll_) > (int32_t)period) nextPoll_ = now + period;  // fell behind
    int t = primaryTarget();
    if (t >= 0) request(REQ_VALUES, COMM_GET_VALUES, (uint8_t)t, replyMs, 0);
    wantSecond_ = dual();
    return;
  }

  if (wantSecond_) {
    wantSecond_ = false;
    int t = secondaryTarget();
    if (t >= 0) request(REQ_VALUES, COMM_GET_VALUES, (uint8_t)t, replyMs, 1);
    return;
  }

  if (!fw_.valid && connected(now) && fwTries_ < 5 && now - lastFwReq_ > 3000) {
    int t = primaryTarget();
    if (t >= 0) {
      lastFwReq_ = now;
      fwTries_++;
      request(REQ_FW, COMM_FW_VERSION, (uint8_t)t, 2 * replyMs, 0);
    }
  }
}
