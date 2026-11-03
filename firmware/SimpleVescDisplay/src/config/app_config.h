#pragma once
// =============================================================================
//  Application configuration (compile-time defaults)
//  Most of these values can be changed later from the on-screen Settings page;
//  they are only used on first boot or after "Factory reset".
// =============================================================================

#define FW_NAME                 "SimpleVescDisplay"
#define FW_VERSION              "2.0.1"

// ---- Communication ----------------------------------------------------------
#define DEF_COMM_MODE           0          // 0 = UART (default), 1 = CAN
#define DEF_UART_BAUD           115200     // must match VESC Tool > App Settings > UART
#define DEF_CAN_BITRATE         2          // 0=125k 1=250k 2=500k 3=1M (VESC default 500k)
#define DEF_CAN_VESC_ID         255        // 255 = auto detect from CAN status/ping
#define DEF_CAN_OWN_ID          253        // ID used by the display on the CAN bus
#define DEF_SECOND_VESC_ID      255        // 255 = disabled (dual motor setups)
#define DEF_POLL_HZ             10         // telemetry requests per second

// ---- Vehicle (same defaults as the original firmware) ----------------------
#define DEF_MOTOR_POLES         30
#define DEF_WHEEL_DIAMETER_MM   246
#define DEF_GEAR_RATIO_X100     100        // motor turns per wheel turn x100 (hub motor = 1.00)
#define DEF_BATTERY_CELLS       16         // 16S -> 67.2 V full
#define DEF_BATTERY_AH_X10      200        // 20.0 Ah (used for the range estimate)

// ---- Warnings ---------------------------------------------------------------
#define DEF_WARN_SPEED          60         // km/h
#define DEF_FET_TEMP_WARN       50         // C  (yellow)
#define DEF_FET_TEMP_CRIT       80         // C  (red)
#define DEF_MOTOR_TEMP_WARN     80
#define DEF_MOTOR_TEMP_CRIT     120
#define DEF_WARN_DUTY_PCT       90
#define DEF_WARN_BATT_PCT       15

// ---- Display ----------------------------------------------------------------
#define DEF_BRIGHTNESS          255
#define LDR_RAW_BRIGHT          200        // analogRead() at/below this = bright ambient light
#define LDR_RAW_DARK            1000       // analogRead() at/above this = dark
#define BRIGHTNESS_MIN_AUTO     25

// ---- Ride logic -------------------------------------------------------------
#define ODO_SAVE_INTERVAL_KM    1.0f       // save while riding every N km ...
#define ODO_SAVE_ON_STOP_KM     0.01f      // ... and when stopped if at least N km is unsaved
#define LINK_TIMEOUT_MS         1500       // no valid packet for this long -> "NO LINK"
#define AUTO_RETURN_MS          15000      // non-dash pages return to dash while riding

// ---- Chart ------------------------------------------------------------------
#define CHART_POINTS            120
#define CHART_SAMPLE_MS         500        // 120 * 0.5 s = 60 s window

// ---- Debug ------------------------------------------------------------------
#ifndef SVD_DEBUG
  #define SVD_DEBUG             1          // log to the USB serial port (115200 baud)
#endif

#if SVD_DEBUG
  #define LOGF(...)             Serial.printf(__VA_ARGS__)
#else
  #define LOGF(...)             do {} while (0)
#endif
