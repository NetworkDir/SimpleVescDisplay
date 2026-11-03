#pragma once
#include <Arduino.h>

namespace display {

void     begin();                          // panel + touch + backlight (uses cfg)
void     showSplash(const char* footer);   // boot logo, drawn directly (before LVGL)
bool     touchPressed();                   // raw touch state (works without LVGL)
void     lvglBegin();                      // LVGL display + input device
void     applyRotation();                  // cfg.flip
void     applyInvert();                    // cfg.invertColors
void     serviceBacklight(uint32_t nowMs); // auto brightness from the light sensor
void     setDimmed(bool dim);              // idle dimming (first touch only wakes the screen)
bool     dimmed();
bool     calibrateTouch();                 // blocking 4-corner calibration, result stored in cfg
bool     touchIsResistive();
uint16_t ldrRaw();
uint8_t  brightness();

}  // namespace display
