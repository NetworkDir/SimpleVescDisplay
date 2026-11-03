#pragma once
// Shared declarations of the UI modules (not part of the public UI API).
#include <lvgl.h>
#include "../config/board_config.h"
#include "../config/app_config.h"

#if LVGL_VERSION_MAJOR != 9
#error "This firmware needs LVGL 9.x"
#endif

// LVGL 9.6 deprecates lv_obj_add_flag() & co. in favour of setters that 9.1 does not have.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#if !defined(LV_CONF_SVD) || LV_CONF_SVD < 2
#error "Wrong or old lv_conf.h - copy firmware/lv_conf.h of this project to Documents/Arduino/libraries/lv_conf.h"
#endif

// ---- LVGL 9.1 ... 9.6 compatibility ------------------------------------------
#if LVGL_VERSION_MINOR < 3
  #define svd_chart_set_range     lv_chart_set_range
  #define svd_table_set_cell_ctrl lv_table_add_cell_ctrl
#else
  #define svd_chart_set_range     lv_chart_set_axis_range
  #define svd_table_set_cell_ctrl lv_table_set_cell_ctrl
#endif

// ---- Fonts (compiled into the sketch, see src/ui/fonts) -----------------------
LV_FONT_DECLARE(svd_font_12);
LV_FONT_DECLARE(svd_font_14);
LV_FONT_DECLARE(svd_font_16);
LV_FONT_DECLARE(svd_font_20);
LV_FONT_DECLARE(svd_font_24);
LV_FONT_DECLARE(svd_font_28);
LV_FONT_DECLARE(svd_font_32);
LV_FONT_DECLARE(svd_font_speed_120);
LV_FONT_DECLARE(svd_font_speed_170);

// ---- Layout metrics per screen size --------------------------------------------
#if defined(UI_LARGE)            // 480 x 320
  #define F_SPEED   (&svd_font_speed_170)
  #define F_TINY    (&svd_font_14)
  #define F_SMALL   (&svd_font_16)
  #define F_BODY    (&svd_font_20)
  #define F_VALUE   (&svd_font_28)
  #define F_LARGE   (&svd_font_32)
  #define HDR_H     54
  #define FTR_H     72
  #define NAV_H     46
  #define BAR_W     18
  #define PAD       8
  #define BTN_H     44
  #define ROW_H     50
#else                            // 320 x 240
  #define F_SPEED   (&svd_font_speed_120)
  #define F_TINY    (&svd_font_12)
  #define F_SMALL   (&svd_font_14)
  #define F_BODY    (&svd_font_16)
  #define F_VALUE   (&svd_font_20)
  #define F_LARGE   (&svd_font_24)
  #define HDR_H     40
  #define FTR_H     52
  #define NAV_H     36
  #define BAR_W     12
  #define PAD       4
  #define BTN_H     34
  #define ROW_H     40
#endif

// ---- Colours --------------------------------------------------------------------
#define C_BG        lv_color_hex(0x000000)
#define C_PANEL     lv_color_hex(0x161B22)
#define C_PANEL_HI  lv_color_hex(0x2A323D)
#define C_LINE      ui_line_color()      // tinted with the ride mode colour
#define C_ROAD      ui_road_color()
#define C_TEXT      lv_color_hex(0xFFFFFF)
#define C_DIM       lv_color_hex(0x8A94A6)
#define C_ACCENT    ui_accent()          // follows the ride mode (theme)
#define C_GREEN     lv_color_hex(0x2EE66B)
#define C_YELLOW    lv_color_hex(0xFFC81E)
#define C_ORANGE    lv_color_hex(0xFF8A1E)
#define C_RED       lv_color_hex(0xFF3B30)
#define C_REGEN     lv_color_hex(0x00D0FF)
#define C_PURPLE    lv_color_hex(0xB06CFF)

lv_color_t ui_accent();
lv_color_t ui_line_color();
lv_color_t ui_road_color();

enum ScreenId : uint8_t { SCR_DASH = 0, SCR_STATS, SCR_LIVE, SCR_CHART, SCR_SETTINGS, SCR_COUNT };

extern const char* const kDashCellNames[];   // CELL_COUNT entries (ui_dash.cpp)

// ---- ui.cpp helpers -------------------------------------------------------------
lv_obj_t*  ui_make_screen();
lv_obj_t*  ui_make_nav(lv_obj_t* scr, const char* title);  // returns the content area below it
lv_obj_t*  ui_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text);
lv_obj_t*  ui_button(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* user);
void       ui_set_text(lv_obj_t* label, const char* text);   // only redraws on change
void       ui_set_color(lv_obj_t* obj, lv_color_t c);         // text colour, only on change
void       ui_goto(ScreenId id);
ScreenId   ui_current();
void       ui_confirm(const char* title, const char* text, const char* okText, void (*onOk)());
void       ui_info(const char* title, const char* text);
bool       ui_dialog_allowed();   // enough memory for a keyboard / chooser? (shows a toast if not)

// unit helpers
float       ui_speed(float kmh);       // km/h -> selected unit
float       ui_dist(float km);
const char* ui_speed_unit();
const char* ui_dist_unit();
float       ui_temp(float c);
const char* ui_temp_unit();
lv_color_t  ui_temp_color(float c, int warn, int crit);
void        ui_fmt_duration(char* buf, size_t n, uint32_t sec);

// ---- pages ------------------------------------------------------------------------
lv_obj_t* dash_create();
void      dash_update(uint32_t now);
void      dash_open_mode_chooser();
void      dash_close_dialogs();           // ride mode / cell chooser (low memory)
void      settings_close_dialogs();       // keypad / keyboard (low memory)
lv_obj_t* stats_create();
void      stats_update(uint32_t now);
lv_obj_t* live_create();
void      live_update(uint32_t now);
lv_obj_t* chart_create();
void      chart_update(uint32_t now);
void      chart_sample(uint32_t now);     // runs even when the page is hidden
void      chart_on_show();
lv_obj_t* settings_create();              // built on demand, deleted on exit
void      settings_update(uint32_t now);
void      settings_on_leave();
