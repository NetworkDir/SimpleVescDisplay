/**
 * LVGL configuration for SimpleVescDisplay (LVGL 9.1 ... 9.6)
 *
 * PlatformIO : picked up automatically (see platformio.ini).
 * Arduino IDE: copy this file to   Documents/Arduino/libraries/lv_conf.h
 *              (next to the "lvgl" folder - back up an existing lv_conf.h first).
 *
 * Only the options that differ from the LVGL defaults are listed here; everything
 * else uses the defaults from lv_conf_internal.h, so the file works across 9.x.
 */
#if 1 /* Set it to "1" to enable content */

#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_CONF_SVD 2                 /* marker: the firmware checks for this file (version) */

/* Colour (LV_COLOR_DEPTH for 9.1-9.5, LV_COLOR_FORMAT_DEFAULT for 9.6+) */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565

/* Memory: the ESP32 heap through the firmware's out-of-memory guard
 * (src/hal/memguard.cpp: keeps a reserve so a full heap closes a page instead of rebooting) */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CUSTOM
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/* Refresh */
#define LV_DEF_REFR_PERIOD      20    /* ms, max ~50 fps */
#define LV_DPI_DEF              130

/* OS: LVGL runs in loop() only */
#define LV_USE_OS               LV_OS_NONE

/* Software renderer (needs "complex" drawing for arcs, gradients, rounded corners) */
#define LV_USE_DRAW_SW          1
#define LV_DRAW_SW_COMPLEX      1
/* Layers (opacity / transforms) are rendered in small chunks: never grab 24 KB at once */
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE (8 * 1024)

/* Logging / asserts */
#define LV_USE_LOG              0
#define LV_USE_ASSERT_NULL      1
#define LV_USE_ASSERT_MALLOC    1
/* On a failed assert: note the cause for the next boot, log, reboot with a backtrace */
#ifdef __cplusplus
extern "C" {
#endif
void svd_lv_assert_failed(void);
#ifdef __cplusplus
}
#endif
#define LV_ASSERT_HANDLER       svd_lv_assert_failed();

/* Fonts: the firmware ships its own fonts (src/ui/fonts); keep only the default one */
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

/* Theme */
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1

/* Widgets: only what the firmware uses (faster compile, smaller binary) */
#define LV_USE_ANIMIMG          0
#define LV_USE_ARC              0
#define LV_USE_CALENDAR         0
#define LV_USE_CANVAS           0
#define LV_USE_CHECKBOX         0
#define LV_USE_DROPDOWN         0
#define LV_USE_IMAGEBUTTON      0
#define LV_USE_KEYBOARD         1
#define LV_USE_LED              0
#define LV_USE_LIST             0
#define LV_USE_MENU             0
#define LV_USE_ROLLER           0
#define LV_USE_SCALE            0
#define LV_USE_SPAN             0
#define LV_USE_SPINBOX          0
#define LV_USE_SPINNER          0
#define LV_USE_TABVIEW          0
#define LV_USE_TEXTAREA         1
#define LV_USE_TILEVIEW         0
#define LV_USE_WIN              0
#define LV_USE_GRID             0
#define LV_USE_OBSERVER         0

/* Unused heavy features */
#define LV_USE_SYSMON           0
#define LV_USE_PERF_MONITOR     0
#define LV_USE_MEM_MONITOR      0
#define LV_USE_SNAPSHOT         0
#define LV_USE_FS_STDIO         0
#define LV_USE_LODEPNG          0
#define LV_USE_TJPGD            0
#define LV_USE_BUILD_EXAMPLES   0
#define LV_BUILD_EXAMPLES       0
#define LV_USE_DEMO_WIDGETS     0
#define LV_USE_DEMO_BENCHMARK   0

#endif /* LV_CONF_H */

#endif /* "Content enable" */
