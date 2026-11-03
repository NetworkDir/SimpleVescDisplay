#pragma once
#include <Arduino.h>
#include "vesc_protocol.h"
#include "vesc_link.h"

class CanLink;

enum class DataSource : uint8_t { NONE, POLL, CAN_STATUS };
enum class ScanState : uint8_t { IDLE, RUNNING, DONE };

struct VescState {
  vesc::Values v;
  bool       valid = false;     // at least one sample received
  uint32_t   updatedMs = 0;     // time of the last sample
  uint32_t   seq = 0;           // incremented on every new sample
  DataSource source = DataSource::NONE;
};

// Owns the active link (UART / CAN / demo), schedules requests and keeps the
// latest telemetry of the primary and (optional) secondary VESC.
class Telemetry {
 public:
  void begin();
  void update(uint32_t nowMs);
  void settingsChanged();       // poll rate / VESC ids changed at runtime

  bool connected(uint32_t nowMs) const;
  bool dual() const;
  const VescState& primary() const { return vesc_[0]; }
  const VescState& secondary() const { return vesc_[1]; }
  const vesc::FwInfo& fw() const { return fw_; }
  const char* linkName() const { return link_ ? link_->name() : "-"; }
  int  canPrimaryId() const { return primaryCanId_; }
  bool isCan() const { return can_ != nullptr; }
  bool bridgeActive() const { return bridge_; }
  bool canBusOff() const;

  uint32_t rxPackets() const { return rxPackets_; }
  uint32_t timeouts() const { return timeouts_; }
  uint32_t errors() const;
  uint16_t latencyMs() const { return latency_; }

  // ---- jobs for the ride modes (sent to the primary VESC between polls) ----
  bool queueReadMcconfTemp();
  bool queueWriteMcconfTemp(const vesc::McconfTemp& m);  // forwarded to all VESCs on CAN
  bool queueLisp(const char* expression);                // no reply expected
  bool jobBusy() const { return jobQueued_ || pending_ == REQ_JOB; }
  const vesc::McconfTemp& mcconf() const { return mcconf_; }
  uint32_t mcconfSeq() const { return mcconfSeq_; }      // ++ for every GET_MCCONF_TEMP reply
  uint32_t setAckSeq() const { return setAckSeq_; }      // ++ for every SET_MCCONF_TEMP ack
  uint32_t jobFailSeq() const { return jobFailSeq_; }    // ++ when a job got no reply

  void startScan();
  ScanState scanState() const { return scanState_; }
  uint8_t scanCount() const { return scanCount_; }
  const uint8_t* scanIds() const { return scanIds_; }
  void clearScan() { scanState_ = ScanState::IDLE; }

 private:
  enum Req : uint8_t { REQ_NONE, REQ_VALUES, REQ_FW, REQ_PING_CAN, REQ_JOB };

  bool queueJob(const uint8_t* payload, size_t len, uint8_t replyId);  // replyId 0xFF = none
  uint8_t  jobBuf_[64];
  size_t   jobLen_ = 0;
  uint8_t  jobReply_ = 0xFF;
  bool     jobQueued_ = false;
  vesc::McconfTemp mcconf_;
  uint32_t mcconfSeq_ = 0, setAckSeq_ = 0, jobFailSeq_ = 0;

  static void onPacketThunk(void* ctx, uint8_t src, const uint8_t* p, size_t len);
  void onPacket(uint8_t src, const uint8_t* p, size_t len);
  bool request(Req kind, uint8_t cmd, uint8_t target, uint32_t timeoutMs, int idx);
  void apply(int idx, const vesc::Values& v, DataSource src, uint32_t now);
  void resolveCanTarget(uint32_t now);
  int  primaryTarget() const;   // -1 = not known yet
  int  secondaryTarget() const; // -1 = disabled
  void noteToolRequest(const uint8_t* p, size_t len, uint32_t now);
  bool toolAwaits(uint8_t cmd, uint32_t now);

  // Commands VESC Tool sent through the bridge and still waits for: a reply with the same
  // id goes to VESC Tool even when it also answers one of the display's own requests.
  struct ToolReq {
    uint8_t  cmd;
    uint32_t at;
  };
  ToolReq   toolReq_[8] = {};
  uint8_t   toolReqNext_ = 0;

  VescLink* link_ = nullptr;
  CanLink*  can_ = nullptr;
  bool      bridge_ = false;

  VescState vesc_[2];
  uint32_t  lastPoll_[2] = {0, 0};
  vesc::FwInfo fw_;

  Req      pending_ = REQ_NONE;
  int      pendingIdx_ = 0;
  uint32_t sentAt_ = 0;
  uint32_t deadline_ = 0;
  uint32_t nextPoll_ = 0;
  bool     wantSecond_ = false;
  uint32_t lastFwReq_ = 0;
  uint8_t  fwTries_ = 0;

  int      primaryCanId_ = -1;
  uint32_t lastAutoScan_ = 0;
  bool     autoScanActive_ = false;
  uint32_t lastStatusApply_[2] = {0, 0};

  uint32_t rxPackets_ = 0;
  uint32_t timeouts_ = 0;
  uint32_t parseErrors_ = 0;
  uint16_t latency_ = 0;

  ScanState scanState_ = ScanState::IDLE;
  uint8_t  scanIds_[16];
  uint8_t  scanCount_ = 0;
};

extern Telemetry telemetry;
