#pragma once
#include <Arduino.h>

void uiBegin();                                   // build the screens (after display::lvglBegin)
void uiUpdate(uint32_t nowMs);                    // call from loop(), before lv_timer_handler()
void uiToast(const char* text, uint32_t rgb = 0xFFFFFF, uint32_t ms = 2500);
