#pragma once
#include <Arduino.h>

// Ride modes ECO / DRIVE / SPORT / SPORT+.
//
// A mode scales the VESC motor current and regen current and can cap the speed.
// The limits are sent with COMM_SET_MCCONF_TEMP and are NOT stored in the VESC:
// after a VESC restart its own configuration is active again (the display re-applies
// the selected mode once it reconnects).
//
//  * the VESC's current limits are read first; duty, power and battery current
//    limits are sent back unchanged - only the current scales and the max ERPM change
//  * the speed cap never exceeds the VESC's own ERPM limit
//  * modes can only be changed while standing still
//  * every few seconds the active limits are read back and re-applied if needed
//  * traction control (multi-motor ADC app) is switched with a Lisp command,
//    which only exists on VESC firmware 7.00.2 and newer
enum class ModeState : uint8_t { OFF, WAITING, READING, APPLYING, ACTIVE, FAILED };

void        rideModesUpdate(uint32_t nowMs);
bool        rideModeSelect(uint8_t mode, const char** whyNot);  // false + reason if refused
uint8_t     rideModeActive();                                     // mode currently applied (or selected)
ModeState   rideModeState();
const char* rideModeName(uint8_t mode);
uint32_t    rideModeColor(uint8_t mode);                          // 0xRRGGBB
bool        rideModeTcsSupported();                               // VESC firmware can switch TC
void        rideModesParamsChanged();                             // re-apply (when stopped)
const char* rideModeStateText();
