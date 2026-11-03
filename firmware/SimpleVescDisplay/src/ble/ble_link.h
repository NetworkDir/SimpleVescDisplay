#pragma once
#include <Arduino.h>
#include "../vesc/vesc_link.h"

// Display -> VESC over Bluetooth LE: connects to a VESC BLE module (NRF51/52 "VESC BLE
// UART", VESC Express, ...) like VESC Tool does. Useful when the VESC UART port is
// already taken by the BLE module.
//
// The nearest VESC BLE device that answers VESC packets is bound to the display (address
// stored in settings); afterwards only that device is used until "Forget" is pressed.
// Connecting/discovery runs in its own task, so the UI never blocks.
class BleLink : public VescLink {
 public:
  enum class State : uint8_t { IDLE, SCANNING, CONNECTING, CONNECTED };

  bool begin() override;
  void end() override {}
  void poll(uint32_t nowMs) override;
  bool send(uint8_t targetId, const uint8_t* payload, size_t len) override;
  const char* name() const override { return "BLE"; }
  uint32_t errorCount() const override { return parser_.crcErrors(); }
  uint32_t replyTimeoutMs() const override { return 450; }

  static State state();
  static const char* stateName();
  static const char* peerAddress();   // "" when unknown

 private:
  vesc::FrameParser parser_;
  uint8_t chunk_[128];
};
