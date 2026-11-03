#pragma once
// =============================================================================
//  Minimal, non-blocking implementation of the VESC communication protocol.
//  Packet format (UART):  [2][len][payload...][crc16 hi][crc16 lo][3]
//                         [3][len hi][len lo][payload...][crc hi][crc lo][3]
//  The same payload format is used inside CAN buffer transfers.
// =============================================================================
#include <stdint.h>
#include <stddef.h>

namespace vesc {

// ---- COMM_PACKET_ID (subset) -------------------------------------------------
enum : uint8_t {
  COMM_FW_VERSION      = 0,
  COMM_GET_VALUES      = 4,
  COMM_ALIVE           = 30,
  COMM_FORWARD_CAN     = 34,
  COMM_SET_MCCONF_TEMP = 48,   // temporary (RAM only) motor limits
  COMM_PING_CAN        = 62,
  COMM_GET_MCCONF_TEMP = 91,
  COMM_LISP_REPL_CMD   = 138,
};

// Motor limits that can be changed temporarily (COMM_GET/SET_MCCONF_TEMP)
struct McconfTemp {
  float currentMinScale = 1, currentMaxScale = 1;  // brake / drive current scale 0..1
  float minErpm = 0, maxErpm = 0;
  float minDuty = 0, maxDuty = 0;
  float wattMin = 0, wattMax = 0;
  float inCurrentMin = 0, inCurrentMax = 0;
  bool  hasInCurrent = false;  // older firmware does not report/accept battery current limits
};

// ---- CAN_PACKET_ID (subset) --------------------------------------------------
enum : uint8_t {
  CAN_PACKET_FILL_RX_BUFFER       = 5,
  CAN_PACKET_FILL_RX_BUFFER_LONG  = 6,
  CAN_PACKET_PROCESS_RX_BUFFER    = 7,
  CAN_PACKET_PROCESS_SHORT_BUFFER = 8,
  CAN_PACKET_STATUS               = 9,
  CAN_PACKET_STATUS_2             = 14,
  CAN_PACKET_STATUS_3             = 15,
  CAN_PACKET_STATUS_4             = 16,
  CAN_PACKET_PING                 = 17,
  CAN_PACKET_PONG                 = 18,
  CAN_PACKET_STATUS_5             = 27,
  CAN_PACKET_STATUS_6             = 58,
};

static constexpr size_t MAX_PAYLOAD = 512;

// Telemetry of one VESC (COMM_GET_VALUES)
struct Values {
  float    tempFet      = 0;   // C
  float    tempMotor    = 0;   // C
  float    currentMotor = 0;   // A (phase current, averaged)
  float    currentIn    = 0;   // A (battery current, averaged)
  float    duty         = 0;   // -1..1
  float    erpm         = 0;
  float    voltage      = 0;   // V
  float    ah           = 0;
  float    ahCharged    = 0;
  float    wh           = 0;
  float    whCharged    = 0;
  int32_t  tacho        = 0;
  int32_t  tachoAbs     = 0;
  uint8_t  fault        = 0;
  uint8_t  controllerId = 0;
  bool     hasTachoAbs  = true;  // false when the data came from CAN status messages
};

struct FwInfo {
  bool    valid = false;
  uint8_t major = 0;
  uint8_t minor = 0;
  char    hw[24] = {0};
};

uint16_t crc16(const uint8_t* buf, size_t len);

// Builds a complete UART frame. Returns the frame length (0 if it does not fit).
size_t buildFrame(const uint8_t* payload, size_t len, uint8_t* out, size_t outSize);

// Parses the payload of a COMM_GET_VALUES reply (payload[0] == COMM_GET_VALUES).
bool parseValues(const uint8_t* payload, size_t len, Values& out);

// Parses the payload of a COMM_FW_VERSION reply.
bool parseFwVersion(const uint8_t* payload, size_t len, FwInfo& out);

// COMM_GET_MCCONF_TEMP reply -> limits
bool parseMcconfTemp(const uint8_t* payload, size_t len, McconfTemp& out);

// Builds a COMM_SET_MCCONF_TEMP payload (not stored to flash). Returns its length.
size_t buildSetMcconfTemp(const McconfTemp& m, bool forwardCan, uint8_t* out);

// VESC "float32_auto" encoding (it is IEEE-754 single precision, big endian)
float getF32Auto(const uint8_t* b, int32_t& idx);
void  putF32Auto(uint8_t* b, float v, int32_t& idx);

const char* faultName(uint8_t fault);

// Big-endian helpers (VESC byte order)
int16_t  getI16(const uint8_t* b, int32_t& idx);
int32_t  getI32(const uint8_t* b, int32_t& idx);
uint16_t getU16(const uint8_t* b, int32_t& idx);
void     putI32(uint8_t* b, int32_t v, int32_t& idx);
void     putI16(uint8_t* b, int16_t v, int32_t& idx);

// Byte-stream decoder for UART frames
class FrameParser {
 public:
  // Feed one byte. Returns true when a complete, CRC-valid packet is available.
  bool feed(uint8_t b);
  const uint8_t* payload() const { return buf_; }
  size_t length() const { return len_; }
  uint32_t crcErrors() const { return crcErrors_; }
  void reset() { state_ = WAIT_START; }

 private:
  enum State : uint8_t { WAIT_START, LEN_HI, LEN_LO, PAYLOAD, CRC_HI, CRC_LO, END };
  State    state_ = WAIT_START;
  bool     longFrame_ = false;
  size_t   len_ = 0;
  size_t   pos_ = 0;
  uint16_t crc_ = 0;
  uint32_t crcErrors_ = 0;
  uint8_t  buf_[MAX_PAYLOAD];
};

}  // namespace vesc
