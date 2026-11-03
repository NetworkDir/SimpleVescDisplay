#pragma once
// =============================================================================
//  Board selection & pin map
// =============================================================================
//
//  PlatformIO selects the board with a build flag (see platformio.ini).
//  Arduino IDE users: uncomment EXACTLY ONE of the lines below.
//
#if !defined(BOARD_ESP32_2432S028R) && !defined(BOARD_ESP32_2432S028R_ST7789) && \
    !defined(BOARD_ESP32_3248S035R) && !defined(BOARD_ESP32_3248S035C)

#define BOARD_ESP32_2432S028R            // 2.8"  320x240 ILI9341, resistive touch (original "CYD")
//#define BOARD_ESP32_2432S028R_ST7789   // 2.8"  320x240 ST7789 variant (CYD with 2 USB ports)
//#define BOARD_ESP32_3248S035R          // 3.5"  480x320 ST7796, resistive touch (XPT2046)
//#define BOARD_ESP32_3248S035C          // 3.5"  480x320 ST7796, capacitive touch (GT911)

#endif

// -----------------------------------------------------------------------------
//  ESP32-2432S028R  (2.8", 320x240)
// -----------------------------------------------------------------------------
#if defined(BOARD_ESP32_2432S028R) || defined(BOARD_ESP32_2432S028R_ST7789)

  #if defined(BOARD_ESP32_2432S028R_ST7789)
    #define BOARD_NAME          "ESP32-2432S028R (ST7789)"
    #define TFT_PANEL_ST7789    1
    #define TFT_INVERT_DEFAULT  false
  #else
    #define BOARD_NAME          "ESP32-2432S028R"
    #define TFT_PANEL_ILI9341   1
    #define TFT_INVERT_DEFAULT  false
  #endif

  #define UI_SMALL              1        // 320x240 layout
  #define TFT_NATIVE_WIDTH      240      // panel memory size in portrait
  #define TFT_NATIVE_HEIGHT     320
  #define TFT_ROTATION          1        // landscape (flip setting adds 2)

  #define PIN_TFT_SCLK          14
  #define PIN_TFT_MOSI          13
  #define PIN_TFT_MISO          12
  #define PIN_TFT_CS            15
  #define PIN_TFT_DC            2
  #define PIN_TFT_RST           -1
  #define PIN_TFT_BL            21

  // XPT2046 sits on its own pins -> bit-banged, keeps VSPI free for the SD card
  #define TOUCH_XPT2046_SOFTSPI 1
  #define PIN_TOUCH_SCLK        25
  #define PIN_TOUCH_MOSI        32
  #define PIN_TOUCH_MISO        39
  #define PIN_TOUCH_CS          33
  #define PIN_TOUCH_IRQ         36
  #define TOUCH_RAW_X_MIN       300
  #define TOUCH_RAW_X_MAX       3900
  #define TOUCH_RAW_Y_MIN       200
  #define TOUCH_RAW_Y_MAX       3700

  // VESC link on connector CN1 (same pins as the original firmware)
  //   UART: RX <- VESC TX, TX -> VESC RX
  //   CAN : RX <- transceiver RXD/R, TX -> transceiver TXD/D
  #ifndef PIN_VESC_RX
    #define PIN_VESC_RX         22
  #endif
  #ifndef PIN_VESC_TX
    #define PIN_VESC_TX         27
  #endif

// -----------------------------------------------------------------------------
//  ESP32-3248S035R / ESP32-3248S035C  (3.5", 480x320)
// -----------------------------------------------------------------------------
#elif defined(BOARD_ESP32_3248S035R) || defined(BOARD_ESP32_3248S035C)

  #define UI_LARGE              1        // 480x320 layout
  #define TFT_PANEL_ST7796      1
  #define TFT_INVERT_DEFAULT    false
  #define TFT_NATIVE_WIDTH      320
  #define TFT_NATIVE_HEIGHT     480
  #define TFT_ROTATION          1

  #define PIN_TFT_SCLK          14
  #define PIN_TFT_MOSI          13
  #define PIN_TFT_MISO          12
  #define PIN_TFT_CS            15
  #define PIN_TFT_DC            2
  #define PIN_TFT_RST           -1
  #define PIN_TFT_BL            27

  #if defined(BOARD_ESP32_3248S035C)
    #define BOARD_NAME          "ESP32-3248S035C"
    #define TOUCH_GT911         1
    #define PIN_TOUCH_SDA       33
    #define PIN_TOUCH_SCL       32
    #define PIN_TOUCH_INT       21
    #define PIN_TOUCH_RST       25
  #else
    #define BOARD_NAME          "ESP32-3248S035R"
    // XPT2046 shares the display SPI bus
    #define TOUCH_XPT2046_SHARED 1
    #define PIN_TOUCH_CS        33
    #define PIN_TOUCH_IRQ       36
    #define TOUCH_RAW_X_MIN     300
    #define TOUCH_RAW_X_MAX     3900
    #define TOUCH_RAW_Y_MIN     200
    #define TOUCH_RAW_Y_MAX     3700
  #endif

  // VESC link on connector P3 (GND / IO35 / IO22 / IO21). IO35 is input-only -> RX.
  // IO21 is left free (GT911 interrupt on the "C" board).
  #ifndef PIN_VESC_RX
    #define PIN_VESC_RX         35
  #endif
  #ifndef PIN_VESC_TX
    #define PIN_VESC_TX         22
  #endif

#else
  #error "No board selected - see src/config/board_config.h"
#endif

// -----------------------------------------------------------------------------
//  Peripherals common to all supported Sunton boards
// -----------------------------------------------------------------------------
#define PIN_LDR                 34       // light sensor (analog)
#define PIN_LED_R               4        // RGB LED, active low
#define PIN_LED_G               16
#define PIN_LED_B               17
#define PIN_SPEAKER             26       // speaker connector (via on-board amplifier)
#define PIN_SD_CS               5        // micro SD on VSPI
#define PIN_SD_SCLK             18
#define PIN_SD_MOSI             23
#define PIN_SD_MISO             19

#if defined(UI_LARGE)
  #define SCREEN_W              480
  #define SCREEN_H              320
#else
  #define SCREEN_W              320
  #define SCREEN_H              240
#endif

// SPI write clock for the panel. 40 MHz is safe on every board tested;
// lower it (e.g. 27000000) if you see noise/garbage on the screen.
#ifndef TFT_SPI_FREQ
  #define TFT_SPI_FREQ          40000000
#endif
