#include "vesc_protocol.h"
#include <string.h>

namespace vesc {

uint16_t crc16(const uint8_t* buf, size_t len) {
  // CRC-16/XMODEM (poly 0x1021, init 0) - same as the VESC firmware
  uint16_t crc = 0;
  while (len--) {
    crc ^= (uint16_t)(*buf++) << 8;
    for (uint8_t i = 0; i < 8; i++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

size_t buildFrame(const uint8_t* payload, size_t len, uint8_t* out, size_t outSize) {
  if (len == 0 || len > MAX_PAYLOAD) return 0;
  const size_t header = (len <= 255) ? 2 : 3;
  const size_t total = header + len + 3;
  if (total > outSize) return 0;

  size_t i = 0;
  if (header == 2) {
    out[i++] = 2;
    out[i++] = (uint8_t)len;
  } else {
    out[i++] = 3;
    out[i++] = (uint8_t)(len >> 8);
    out[i++] = (uint8_t)(len & 0xFF);
  }
  memcpy(out + i, payload, len);
  i += len;
  const uint16_t crc = crc16(payload, len);
  out[i++] = (uint8_t)(crc >> 8);
  out[i++] = (uint8_t)(crc & 0xFF);
  out[i++] = 3;
  return i;
}

int16_t getI16(const uint8_t* b, int32_t& idx) {
  int16_t v = (int16_t)(((uint16_t)b[idx] << 8) | b[idx + 1]);
  idx += 2;
  return v;
}

uint16_t getU16(const uint8_t* b, int32_t& idx) {
  uint16_t v = ((uint16_t)b[idx] << 8) | b[idx + 1];
  idx += 2;
  return v;
}

int32_t getI32(const uint8_t* b, int32_t& idx) {
  int32_t v = (int32_t)(((uint32_t)b[idx] << 24) | ((uint32_t)b[idx + 1] << 16) |
                        ((uint32_t)b[idx + 2] << 8) | (uint32_t)b[idx + 3]);
  idx += 4;
  return v;
}

void putI32(uint8_t* b, int32_t v, int32_t& idx) {
  b[idx++] = (uint8_t)(v >> 24);
  b[idx++] = (uint8_t)(v >> 16);
  b[idx++] = (uint8_t)(v >> 8);
  b[idx++] = (uint8_t)v;
}

void putI16(uint8_t* b, int16_t v, int32_t& idx) {
  b[idx++] = (uint8_t)(v >> 8);
  b[idx++] = (uint8_t)v;
}

bool parseValues(const uint8_t* p, size_t len, Values& v) {
  // Layout: https://github.com/vedderb/bldc/blob/master/comm/commands.c (COMM_GET_VALUES)
  static constexpr size_t MIN_LEN = 54;  // up to and including the fault code
  if (len < MIN_LEN || p[0] != COMM_GET_VALUES) return false;

  int32_t i = 1;
  v.tempFet      = getI16(p, i) / 10.0f;
  v.tempMotor    = getI16(p, i) / 10.0f;
  v.currentMotor = getI32(p, i) / 100.0f;
  v.currentIn    = getI32(p, i) / 100.0f;
  i += 8;  // avg id, avg iq
  v.duty         = getI16(p, i) / 1000.0f;
  v.erpm         = (float)getI32(p, i);
  v.voltage      = getI16(p, i) / 10.0f;
  v.ah           = getI32(p, i) / 10000.0f;
  v.ahCharged    = getI32(p, i) / 10000.0f;
  v.wh           = getI32(p, i) / 10000.0f;
  v.whCharged    = getI32(p, i) / 10000.0f;
  v.tacho        = getI32(p, i);
  v.tachoAbs     = getI32(p, i);
  v.fault        = p[i++];
  v.hasTachoAbs  = true;
  if ((size_t)i + 5 <= len) {
    i += 4;  // pid position
    v.controllerId = p[i++];
  }
  return true;
}

bool parseFwVersion(const uint8_t* p, size_t len, FwInfo& fw) {
  if (len < 3 || p[0] != COMM_FW_VERSION) return false;
  fw.major = p[1];
  fw.minor = p[2];
  fw.hw[0] = 0;
  size_t n = 0;
  for (size_t i = 3; i < len && n < sizeof(fw.hw) - 1; i++) {
    if (p[i] == 0) break;
    fw.hw[n++] = (char)p[i];
  }
  fw.hw[n] = 0;
  fw.valid = true;
  return true;
}

float getF32Auto(const uint8_t* b, int32_t& idx) {
  const uint32_t u = ((uint32_t)b[idx] << 24) | ((uint32_t)b[idx + 1] << 16) | ((uint32_t)b[idx + 2] << 8) | b[idx + 3];
  idx += 4;
  float f;
  memcpy(&f, &u, sizeof(f));
  return f;
}

void putF32Auto(uint8_t* b, float v, int32_t& idx) {
  if (v > -1.5e-38f && v < 1.5e-38f) v = 0.0f;  // the VESC flushes subnormals to 0 as well
  uint32_t u;
  memcpy(&u, &v, sizeof(u));
  b[idx++] = (uint8_t)(u >> 24);
  b[idx++] = (uint8_t)(u >> 16);
  b[idx++] = (uint8_t)(u >> 8);
  b[idx++] = (uint8_t)u;
}

bool parseMcconfTemp(const uint8_t* p, size_t len, McconfTemp& m) {
  // [id] 8 (older FW) or 10 x float32_auto, then setup values we do not need
  if (len < 33 || p[0] != COMM_GET_MCCONF_TEMP) return false;
  int32_t i = 1;
  m.currentMinScale = getF32Auto(p, i);
  m.currentMaxScale = getF32Auto(p, i);
  m.minErpm = getF32Auto(p, i);
  m.maxErpm = getF32Auto(p, i);
  m.minDuty = getF32Auto(p, i);
  m.maxDuty = getF32Auto(p, i);
  m.wattMin = getF32Auto(p, i);
  m.wattMax = getF32Auto(p, i);
  m.hasInCurrent = len >= 41;
  if (m.hasInCurrent) {
    m.inCurrentMin = getF32Auto(p, i);
    m.inCurrentMax = getF32Auto(p, i);
  }
  return true;
}

size_t buildSetMcconfTemp(const McconfTemp& m, bool forwardCan, uint8_t* p) {
  int32_t i = 0;
  p[i++] = COMM_SET_MCCONF_TEMP;
  p[i++] = 0;                  // store: never write to flash
  p[i++] = forwardCan ? 1 : 0; // apply to every VESC on the CAN bus (dual motor)
  p[i++] = 1;                  // ack
  p[i++] = 0;                  // values are already per controller
  putF32Auto(p, m.currentMinScale, i);
  putF32Auto(p, m.currentMaxScale, i);
  putF32Auto(p, m.minErpm, i);
  putF32Auto(p, m.maxErpm, i);
  putF32Auto(p, m.minDuty, i);
  putF32Auto(p, m.maxDuty, i);
  putF32Auto(p, m.wattMin, i);
  putF32Auto(p, m.wattMax, i);
  if (m.hasInCurrent) {  // optional, sent back unchanged
    putF32Auto(p, m.inCurrentMin, i);
    putF32Auto(p, m.inCurrentMax, i);
  }
  return (size_t)i;
}

const char* faultName(uint8_t f) {
  static const char* const names[] = {
    "NONE",                 // 0
    "OVER VOLTAGE",         // 1
    "UNDER VOLTAGE",        // 2
    "DRV",                  // 3
    "ABS OVER CURRENT",     // 4
    "OVER TEMP FET",        // 5
    "OVER TEMP MOTOR",      // 6
    "GATE DRV OVER VOLT",   // 7
    "GATE DRV UNDER VOLT",  // 8
    "MCU UNDER VOLTAGE",    // 9
    "WATCHDOG RESET",       // 10
    "ENCODER SPI",          // 11
    "SINCOS BELOW MIN",     // 12
    "SINCOS ABOVE MAX",     // 13
    "FLASH CORRUPTION",     // 14
    "CURR SENSOR 1 OFFS",   // 15
    "CURR SENSOR 2 OFFS",   // 16
    "CURR SENSOR 3 OFFS",   // 17
    "UNBALANCED CURRENTS",  // 18
    "BRK",                  // 19
    "RESOLVER LOT",         // 20
    "RESOLVER DOS",         // 21
    "RESOLVER LOS",         // 22
    "FLASH CORR APP CFG",   // 23
    "FLASH CORR MC CFG",    // 24
    "ENCODER NO MAGNET",    // 25
    "MAGNET TOO STRONG",    // 26
    "PHASE FILTER",         // 27
    "ENCODER FAULT",        // 28
    "LV OUTPUT FAULT",      // 29
  };
  if (f < sizeof(names) / sizeof(names[0])) return names[f];
  return "UNKNOWN";
}

// ---------------------------------------------------------------------------

bool FrameParser::feed(uint8_t b) {
  switch (state_) {
    case WAIT_START:
      if (b == 2) {
        longFrame_ = false;
        state_ = LEN_LO;
      } else if (b == 3) {
        longFrame_ = true;
        state_ = LEN_HI;
      }
      return false;

    case LEN_HI:
      len_ = (size_t)b << 8;
      state_ = LEN_LO;
      return false;

    case LEN_LO:
      len_ = longFrame_ ? (len_ | b) : b;
      if (len_ == 0 || len_ > MAX_PAYLOAD) {
        state_ = WAIT_START;
      } else {
        pos_ = 0;
        state_ = PAYLOAD;
      }
      return false;

    case PAYLOAD:
      buf_[pos_++] = b;
      if (pos_ >= len_) state_ = CRC_HI;
      return false;

    case CRC_HI:
      crc_ = (uint16_t)b << 8;
      state_ = CRC_LO;
      return false;

    case CRC_LO:
      crc_ |= b;
      state_ = END;
      return false;

    case END:
      state_ = WAIT_START;
      if (b == 3 && crc16(buf_, len_) == crc_) return true;
      crcErrors_++;
      return false;
  }
  state_ = WAIT_START;
  return false;
}

}  // namespace vesc
