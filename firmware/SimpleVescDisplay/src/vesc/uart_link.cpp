#include "uart_link.h"
#include <string.h>

bool UartLink::begin() {
  // RX: a long VESC Tool reply must survive one slow UI frame.
  // TX: buffered, so forwarding VESC Tool packets never blocks the loop (~45 ms per 512 bytes).
  serial_.setRxBufferSize(2048);
  serial_.setTxBufferSize(1024);
  serial_.begin(baud_, SERIAL_8N1, rx_, tx_);
  parser_.reset();
  return true;
}

void UartLink::end() {
  serial_.end();
}

void UartLink::poll(uint32_t) {
  // Bounded loop so a flood of garbage can never starve the UI.
  int n = serial_.available();
  if (n > 600) n = 600;
  while (n-- > 0) {
    int c = serial_.read();
    if (c < 0) break;
    if (parser_.feed((uint8_t)c)) dispatch(VESC_TARGET_LOCAL, parser_.payload(), parser_.length());
  }
}

bool UartLink::send(uint8_t targetId, const uint8_t* payload, size_t len) {
  static uint8_t body[vesc::MAX_PAYLOAD];               // static: keep the loop task stack small
  static uint8_t frame[vesc::MAX_PAYLOAD + 8];
  if (targetId != VESC_TARGET_LOCAL) {
    if (len + 2 > sizeof(body)) return false;
    body[0] = vesc::COMM_FORWARD_CAN;
    body[1] = targetId;
    memcpy(body + 2, payload, len);
    payload = body;
    len += 2;
  }
  size_t n = vesc::buildFrame(payload, len, frame, sizeof(frame));
  if (!n) return false;
  return serial_.write(frame, n) == n;
}
