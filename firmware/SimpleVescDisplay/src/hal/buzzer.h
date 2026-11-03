#pragma once
#include <Arduino.h>

// Non-blocking beeper on the speaker connector (needs a small speaker attached).
enum BeepPattern : uint8_t {
  BEEP_SHORT,      // single short beep (speed warning, UI feedback)
  BEEP_WARNING,    // two medium beeps
  BEEP_CRITICAL,   // three long high beeps
  BEEP_DUTY,       // fast double beep, repeated while duty is too high
  BEEP_BOOT,       // rising chirp at start-up
  BEEP_CLICK,      // touch feedback
};

void buzzerBegin();
void buzzerPlay(BeepPattern p);
void buzzerUpdate(uint32_t nowMs);
