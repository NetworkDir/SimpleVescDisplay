#pragma once
#include <Arduino.h>
#include "vesc_link.h"

// VESC over UART - completely non-blocking (bytes are decoded as they arrive).
class UartLink : public VescLink {
 public:
  UartLink(HardwareSerial& serial, uint32_t baud, int rxPin, int txPin)
      : serial_(serial), baud_(baud), rx_(rxPin), tx_(txPin) {}

  bool begin() override;
  void end() override;
  void poll(uint32_t nowMs) override;
  bool send(uint8_t targetId, const uint8_t* payload, size_t len) override;
  const char* name() const override { return "UART"; }
  uint32_t errorCount() const override { return parser_.crcErrors(); }

 private:
  HardwareSerial& serial_;
  uint32_t baud_;
  int rx_, tx_;
  vesc::FrameParser parser_;
};
