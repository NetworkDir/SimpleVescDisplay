#pragma once
#include <stdint.h>
#include <stddef.h>
#include "vesc_protocol.h"

// Target id for the VESC that is physically connected to the UART port.
static constexpr uint8_t VESC_TARGET_LOCAL = 0xFF;

// Abstract transport to one or more VESCs (UART, CAN or simulated).
class VescLink {
 public:
  // srcId: CAN id of the sender if known, VESC_TARGET_LOCAL otherwise
  typedef void (*PacketHandler)(void* ctx, uint8_t srcId, const uint8_t* payload, size_t len);

  virtual ~VescLink() {}
  virtual bool begin() = 0;
  virtual void end() = 0;
  virtual void poll(uint32_t nowMs) = 0;
  // Sends a command payload. For UART, targets other than LOCAL are wrapped in COMM_FORWARD_CAN.
  virtual bool send(uint8_t targetId, const uint8_t* payload, size_t len) = 0;
  virtual const char* name() const = 0;
  virtual uint32_t errorCount() const { return 0; }
  // Time to wait for the reply to a request (Bluetooth needs several connection events).
  virtual uint32_t replyTimeoutMs() const { return 150; }

  void setHandler(PacketHandler h, void* ctx) {
    handler_ = h;
    ctx_ = ctx;
  }

 protected:
  void dispatch(uint8_t src, const uint8_t* p, size_t len) {
    if (handler_) handler_(ctx_, src, p, len);
  }

 private:
  PacketHandler handler_ = nullptr;
  void* ctx_ = nullptr;
};
