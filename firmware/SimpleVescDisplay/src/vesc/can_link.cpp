#include "can_link.h"
#include <string.h>
#include "driver/twai.h"

using namespace vesc;

static constexpr uint32_t STATUS_FRESH_MS = 1000;
static constexpr uint32_t SCAN_TIMEOUT_MS = 3000;

bool CanLink::begin() {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)tx_, (gpio_num_t)rx_, TWAI_MODE_NORMAL);
  g.rx_queue_len = 64;
  g.tx_queue_len = 32;

  static const twai_timing_config_t t125 = TWAI_TIMING_CONFIG_125KBITS();
  static const twai_timing_config_t t250 = TWAI_TIMING_CONFIG_250KBITS();
  static const twai_timing_config_t t500 = TWAI_TIMING_CONFIG_500KBITS();
  static const twai_timing_config_t t1000 = TWAI_TIMING_CONFIG_1MBITS();
  const twai_timing_config_t* t = &t500;
  switch (bitrate_) {
    case 0: t = &t125; break;
    case 1: t = &t250; break;
    case 3: t = &t1000; break;
    default: break;
  }
  static const twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g, t, &f) != ESP_OK) return false;
  if (twai_start() != ESP_OK) {
    twai_driver_uninstall();
    return false;
  }
  started_ = true;
  return true;
}

void CanLink::end() {
  if (!started_) return;
  twai_stop();
  twai_driver_uninstall();
  started_ = false;
}

bool CanLink::tx(uint32_t id, const uint8_t* data, uint8_t len) {
  if (!started_ || len > 8) return false;
  twai_message_t m;
  memset(&m, 0, sizeof(m));
  m.extd = 1;
  m.identifier = id;
  m.data_length_code = len;
  memcpy(m.data, data, len);
  return twai_transmit(&m, 0) == ESP_OK;
}

bool CanLink::send(uint8_t target, const uint8_t* payload, size_t len) {
  if (target == VESC_TARGET_LOCAL || len == 0 || len > MAX_PAYLOAD) return false;
  uint8_t d[8];

  if (len <= 6) {
    d[0] = ownId_;
    d[1] = 0;  // 0 = process the command and send the reply back to us
    memcpy(d + 2, payload, len);
    return tx(((uint32_t)CAN_PACKET_PROCESS_SHORT_BUFFER << 8) | target, d, (uint8_t)(len + 2));
  }

  size_t i = 0;
  for (; i < len && i < 256; i += 7) {
    uint8_t n = (uint8_t)((len - i) < 7 ? (len - i) : 7);
    d[0] = (uint8_t)i;
    memcpy(d + 1, payload + i, n);
    if (!tx(((uint32_t)CAN_PACKET_FILL_RX_BUFFER << 8) | target, d, n + 1)) return false;
  }
  for (; i < len; i += 6) {
    uint8_t n = (uint8_t)((len - i) < 6 ? (len - i) : 6);
    d[0] = (uint8_t)(i >> 8);
    d[1] = (uint8_t)(i & 0xFF);
    memcpy(d + 2, payload + i, n);
    if (!tx(((uint32_t)CAN_PACKET_FILL_RX_BUFFER_LONG << 8) | target, d, n + 2)) return false;
  }
  const uint16_t crc = crc16(payload, len);
  d[0] = ownId_;
  d[1] = 0;
  d[2] = (uint8_t)(len >> 8);
  d[3] = (uint8_t)(len & 0xFF);
  d[4] = (uint8_t)(crc >> 8);
  d[5] = (uint8_t)(crc & 0xFF);
  return tx(((uint32_t)CAN_PACKET_PROCESS_RX_BUFFER << 8) | target, d, 6);
}

CanLink::StatusSlot* CanLink::slotFor(uint8_t id, bool create) {
  StatusSlot* oldest = &slots_[0];
  for (auto& s : slots_) {
    if (s.used && s.id == id) return &s;
  }
  if (!create) return nullptr;
  for (auto& s : slots_) {
    if (!s.used) {
      oldest = &s;
      break;
    }
    if (s.lastRx < oldest->lastRx) oldest = &s;
  }
  *oldest = StatusSlot();
  oldest->used = true;
  oldest->id = id;
  oldest->v.controllerId = id;
  oldest->v.hasTachoAbs = false;
  return oldest;
}

void CanLink::addFound(uint8_t id) {
  for (uint8_t i = 0; i < foundCount_; i++) {
    if (found_[i] == id) return;
  }
  if (foundCount_ < sizeof(found_)) found_[foundCount_++] = id;
}

void CanLink::handleFrame(uint32_t id, const uint8_t* d, uint8_t len, uint32_t now) {
  const uint8_t pkt = (id >> 8) & 0xFF;
  const uint8_t node = id & 0xFF;
  int32_t i = 0;

  switch (pkt) {
    case CAN_PACKET_STATUS:
      if (len >= 8) {
        StatusSlot* s = slotFor(node, true);
        s->v.erpm = (float)getI32(d, i);
        s->v.currentMotor = getI16(d, i) / 10.0f;
        s->v.duty = getI16(d, i) / 1000.0f;
        s->seen |= 0x01;
        s->lastRx = now;
      }
      break;
    case CAN_PACKET_STATUS_2:
      if (len >= 8) {
        StatusSlot* s = slotFor(node, true);
        s->v.ah = getI32(d, i) / 10000.0f;
        s->v.ahCharged = getI32(d, i) / 10000.0f;
        s->seen |= 0x02;
        s->lastRx = now;
      }
      break;
    case CAN_PACKET_STATUS_3:
      if (len >= 8) {
        StatusSlot* s = slotFor(node, true);
        s->v.wh = getI32(d, i) / 10000.0f;
        s->v.whCharged = getI32(d, i) / 10000.0f;
        s->seen |= 0x04;
        s->lastRx = now;
      }
      break;
    case CAN_PACKET_STATUS_4:
      if (len >= 8) {
        StatusSlot* s = slotFor(node, true);
        s->v.tempFet = getI16(d, i) / 10.0f;
        s->v.tempMotor = getI16(d, i) / 10.0f;
        s->v.currentIn = getI16(d, i) / 10.0f;
        s->seen |= 0x08;
        s->lastRx = now;
      }
      break;
    case CAN_PACKET_STATUS_5:
      if (len >= 6) {
        StatusSlot* s = slotFor(node, true);
        s->v.tacho = getI32(d, i);
        s->v.voltage = getI16(d, i) / 10.0f;
        s->seen |= 0x10;
        s->lastRx = now;
      }
      break;

    case CAN_PACKET_PONG:
      if (node == ownId_ && len >= 1) addFound(d[0]);
      break;

    case CAN_PACKET_FILL_RX_BUFFER:
      if (node == ownId_ && len >= 1) {
        size_t off = d[0], n = len - 1;
        if (off + n <= sizeof(rxBuf_)) memcpy(rxBuf_ + off, d + 1, n);
      }
      break;
    case CAN_PACKET_FILL_RX_BUFFER_LONG:
      if (node == ownId_ && len >= 2) {
        size_t off = ((size_t)d[0] << 8) | d[1], n = len - 2;
        if (off + n <= sizeof(rxBuf_)) memcpy(rxBuf_ + off, d + 2, n);
      }
      break;
    case CAN_PACKET_PROCESS_RX_BUFFER:
      if (node == ownId_ && len >= 6) {
        const uint8_t sender = d[0];
        const size_t plen = ((size_t)d[2] << 8) | d[3];
        const uint16_t crc = ((uint16_t)d[4] << 8) | d[5];
        if (plen > 0 && plen <= sizeof(rxBuf_) && crc16(rxBuf_, plen) == crc) {
          dispatch(sender, rxBuf_, plen);
        } else {
          errors_++;
        }
      }
      break;
    case CAN_PACKET_PROCESS_SHORT_BUFFER:
      if (node == ownId_ && len >= 3) dispatch(d[0], d + 2, len - 2);
      break;
    default:
      break;
  }
}

void CanLink::poll(uint32_t now) {
  if (!started_) return;

  twai_message_t m;
  int budget = 64;
  while (budget-- > 0 && twai_receive(&m, 0) == ESP_OK) {
    if (!m.extd || m.rtr) continue;
    // the DLC field can say up to 15, but a classic CAN frame carries at most 8 bytes
    handleFrame(m.identifier, m.data, m.data_length_code > 8 ? 8 : m.data_length_code, now);
  }

  if (scanning_) {
    while (scanNext_ < 255) {
      if (scanNext_ == ownId_) {
        scanNext_++;
        continue;
      }
      uint8_t d[1] = {ownId_};
      if (!tx(((uint32_t)CAN_PACKET_PING << 8) | (uint8_t)scanNext_, d, 1)) break;
      scanNext_++;
    }
    // all pings queued: wait a little for the last PONGs
    if (scanNext_ >= 255 && scanTail_ == 0) scanTail_ = now + 400;
    if ((scanTail_ && (int32_t)(now - scanTail_) >= 0) || (int32_t)(now - scanDeadline_) >= 0) {
      scanning_ = false;
    }
  }

  if (now - lastHealth_ > 250) {
    lastHealth_ = now;
    twai_status_info_t st;
    if (twai_get_status_info(&st) == ESP_OK) {
      if (st.state == TWAI_STATE_BUS_OFF) {
        if (!busOff_) errors_++;
        busOff_ = true;
        twai_initiate_recovery();
      } else if (st.state == TWAI_STATE_STOPPED) {
        twai_start();
      } else {
        busOff_ = false;
      }
    }
  }
}

void CanLink::startPingScan() {
  foundCount_ = 0;
  scanNext_ = 0;
  scanning_ = true;
  scanTail_ = 0;
  scanDeadline_ = millis() + SCAN_TIMEOUT_MS;
}

bool CanLink::statusFor(uint8_t id, vesc::Values& out, uint32_t now) const {
  for (const auto& s : slots_) {
    if (s.used && s.id == id) {
      // need at least STATUS (speed) and STATUS_5 (voltage) to be useful
      if (now - s.lastRx > STATUS_FRESH_MS || (s.seen & 0x11) != 0x11) return false;
      out = s.v;
      return true;
    }
  }
  return false;
}

int CanLink::firstStatusNode(uint32_t now, uint8_t excludeId) const {
  int best = -1;
  for (const auto& s : slots_) {
    if (!s.used || s.id == excludeId || s.id == ownId_) continue;
    if (now - s.lastRx > STATUS_FRESH_MS) continue;
    if (best < 0 || s.id < best) best = s.id;
  }
  return best;
}
