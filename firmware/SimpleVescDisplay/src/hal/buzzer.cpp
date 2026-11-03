#include "buzzer.h"
#include "../config/board_config.h"

struct Step {
  uint16_t freq;  // 0 = silence
  uint16_t ms;
};

static const Step P_SHORT[]    = {{2600, 60}, {0, 0}};
static const Step P_WARNING[]  = {{2000, 120}, {0, 100}, {2000, 120}, {0, 0}};
static const Step P_CRITICAL[] = {{3000, 250}, {0, 120}, {3000, 250}, {0, 120}, {3000, 250}, {0, 0}};
static const Step P_DUTY[]     = {{3400, 50}, {0, 50}, {3400, 50}, {0, 0}};
static const Step P_BOOT[]     = {{1800, 60}, {2400, 60}, {3000, 80}, {0, 0}};
static const Step P_CLICK[]    = {{4000, 6}, {0, 0}};

static const Step* s_pattern = nullptr;
static uint8_t     s_step = 0;
static uint32_t    s_stepEnd = 0;

void buzzerBegin() {
  pinMode(PIN_SPEAKER, OUTPUT);
  digitalWrite(PIN_SPEAKER, LOW);
}

static void startStep(uint32_t now) {
  const Step& st = s_pattern[s_step];
  if (st.ms == 0) {  // end marker
    noTone(PIN_SPEAKER);
    s_pattern = nullptr;
    return;
  }
  if (st.freq) tone(PIN_SPEAKER, st.freq);
  else noTone(PIN_SPEAKER);
  s_stepEnd = now + st.ms;
}

void buzzerPlay(BeepPattern p) {
  switch (p) {
    case BEEP_SHORT:    s_pattern = P_SHORT; break;
    case BEEP_WARNING:  s_pattern = P_WARNING; break;
    case BEEP_CRITICAL: s_pattern = P_CRITICAL; break;
    case BEEP_DUTY:     s_pattern = P_DUTY; break;
    case BEEP_BOOT:     s_pattern = P_BOOT; break;
    case BEEP_CLICK:    s_pattern = P_CLICK; break;
  }
  s_step = 0;
  startStep(millis());
}

void buzzerUpdate(uint32_t now) {
  if (!s_pattern) return;
  if ((int32_t)(now - s_stepEnd) >= 0) {
    s_step++;
    startStep(now);
  }
}
