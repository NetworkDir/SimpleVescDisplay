#pragma once
#include <Arduino.h>
#include "../vesc/vesc_protocol.h"

// Makes the display behave like a VESC BLE module: VESC Tool (phone / PC) connects
// to the display over BLE and its packets are forwarded to the VESC over UART or CAN.
// Replies from the VESC are sent back, while the display keeps polling its own data.
//
// With pairing enabled the phone must enter the 6 digit PIN shown on the display
// (once - the bond is remembered), so nobody nearby can change the motor config.
class BleBridge {
 public:
  bool begin();
  bool active() const { return active_; }
  bool connected() const;               // VESC Tool connected (and authenticated)
  bool pinRequested() const;            // pairing in progress: show cfg.blePasskey
  void service(uint32_t nowMs);         // push queued notifications

  // Next complete packet received from VESC Tool (payload only, CRC checked).
  bool readPacket(const uint8_t*& payload, size_t& len);
  // Send a VESC reply to VESC Tool.
  void sendPacket(const uint8_t* payload, size_t len);

  void forgetBonds();
  uint32_t rxBytes() const { return rxBytes_; }
  uint32_t txBytes() const { return txBytes_; }

 private:
  bool active_ = false;
  bool wasConnected_ = false;
  vesc::FrameParser parser_;
  uint8_t chunk_[64];
  size_t chunkLen_ = 0, chunkPos_ = 0;
  uint8_t pend_[244];
  size_t pendLen_ = 0;
  uint32_t rxBytes_ = 0, txBytes_ = 0;
};

extern BleBridge bleBridge;
