#pragma once
#include <Arduino.h>

// On-board RGB LED (back of the board), used as a status light:
//   green = OK, yellow = warning, red blinking = critical,
//   blue blinking = no VESC link, purple = demo mode
enum LedColor : uint8_t {
  LED_OFF = 0, LED_RED = 1, LED_GREEN = 2, LED_YELLOW = 3,
  LED_BLUE = 4, LED_PURPLE = 5, LED_CYAN = 6, LED_WHITE = 7
};

void statusLedBegin();
void statusLedSet(LedColor color, bool blink);
void statusLedUpdate(uint32_t nowMs);
