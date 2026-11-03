#pragma once
#include <Arduino.h>

// CSV ride logger on the micro SD card (/svd/ride_NNNN.csv, 5 lines per second).
// Writes are buffered and flushed every few seconds to keep the UI smooth.
enum class SdState : uint8_t { OFF, NO_CARD, READY, LOGGING, ERROR };

void        sdlogUpdate(uint32_t nowMs);
SdState     sdlogState();
const char* sdlogFile();       // current file name ("" if none)
uint32_t    sdlogLines();
