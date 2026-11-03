#pragma once
#include <Arduino.h>
#include "vesc_link.h"

// VESC over CAN using the ESP32's built-in TWAI controller.
// Needs an external 3.3 V CAN transceiver (SN65HVD230, TJA1051T/3, ...).
//
//  * Commands are sent with CAN_PACKET_PROCESS_SHORT_BUFFER and replies are
//    re-assembled from FILL_RX_BUFFER / PROCESS_RX_BUFFER frames.
//  * CAN status broadcasts (STATUS 1..5) are cached for every node; they are
//    used for auto-detecting the VESC id and as a fallback data source.
//  * CAN_PACKET_PING sweeps find every VESC on the bus.
class CanLink : public VescLink {
 public:
  CanLink(int rxPin, int txPin, uint8_t bitrateIdx, uint8_t ownId)
      : rx_(rxPin), tx_(txPin), bitrate_(bitrateIdx), ownId_(ownId) {}

  bool begin() override;
  void end() override;
  void poll(uint32_t nowMs) override;
  bool send(uint8_t targetId, const uint8_t* payload, size_t len) override;
  const char* name() const override { return "CAN"; }
  uint32_t errorCount() const override { return errors_; }

  // Latest data reconstructed from CAN status messages of node `id`.
  bool statusFor(uint8_t id, vesc::Values& out, uint32_t nowMs) const;
  // Lowest node id that sent status messages recently (-1 if none).
  int  firstStatusNode(uint32_t nowMs, uint8_t excludeId) const;

  void startPingScan();
  bool scanning() const { return scanning_; }
  uint8_t foundCount() const { return foundCount_; }
  const uint8_t* foundIds() const { return found_; }

  bool started() const { return started_; }
  bool busOff() const { return busOff_; }

 private:
  struct StatusSlot {
    bool used = false;
    uint8_t id = 0;
    uint8_t seen = 0;        // bit mask of received status frames
    uint32_t lastRx = 0;
    vesc::Values v;
  };

  void handleFrame(uint32_t id, const uint8_t* d, uint8_t len, uint32_t now);
  bool tx(uint32_t id, const uint8_t* data, uint8_t len);
  StatusSlot* slotFor(uint8_t id, bool create);
  void addFound(uint8_t id);

  int rx_, tx_;
  uint8_t bitrate_;
  uint8_t ownId_;
  bool started_ = false;
  bool busOff_ = false;
  uint32_t errors_ = 0;
  uint32_t lastHealth_ = 0;

  uint8_t rxBuf_[vesc::MAX_PAYLOAD];
  StatusSlot slots_[8];

  bool scanning_ = false;
  int scanNext_ = 0;
  uint32_t scanTail_ = 0;
  uint32_t scanDeadline_ = 0;
  uint8_t found_[16];
  uint8_t foundCount_ = 0;
};
