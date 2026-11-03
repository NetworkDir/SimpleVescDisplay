#include "status_led.h"
#include "../config/board_config.h"

static LedColor s_color = LED_OFF;
static bool     s_blink = false;
static uint8_t  s_applied = 0xFF;

static void write(uint8_t rgb) {
  if (rgb == s_applied) return;
  s_applied = rgb;
  // active low
  digitalWrite(PIN_LED_R, (rgb & 1) ? LOW : HIGH);
  digitalWrite(PIN_LED_G, (rgb & 2) ? LOW : HIGH);
  digitalWrite(PIN_LED_B, (rgb & 4) ? LOW : HIGH);
}

void statusLedBegin() {
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  write(0);
}

void statusLedSet(LedColor color, bool blink) {
  s_color = color;
  s_blink = blink;
}

void statusLedUpdate(uint32_t now) {
  bool on = !s_blink || ((now / 250) & 1);
  write(on ? (uint8_t)s_color : 0);
}
