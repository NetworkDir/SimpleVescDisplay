// Trip statistics as a grid of tiles + "Reset trip".
#include "ui_internal.h"
#include <math.h>
#include "ui.h"
#include "../core/ride.h"
#include "../core/settings.h"

enum StatId {
  ST_TRIP, ST_RIDE_TIME, ST_AVG, ST_MAX,
  ST_WH_USED, ST_WH_REGEN, ST_EFF, ST_RANGE,
  ST_MAX_PWR, ST_MAX_REGEN, ST_MAX_BATT_A, ST_MAX_PHASE_A,
  ST_MIN_V, ST_MAX_FET, ST_MAX_MOTOR, ST_AH,
  ST_ODO, ST_SESSION,
  ST_COUNT
};

static const char* const kNames[ST_COUNT] = {
  "TRIP", "RIDE TIME", "AVG SPEED", "MAX SPEED",
  "ENERGY USED", "REGEN", "EFFICIENCY", "RANGE",
  "MAX POWER", "MAX REGEN", "MAX BATT A", "MAX PHASE A",
  "MIN VOLTAGE", "MAX ESC TEMP", "MAX MOTOR TEMP", "AH USED / REGEN",
  "ODOMETER", "ON TIME",
};

static lv_obj_t* s_val[ST_COUNT];

static void doReset() {
  ride.resetTrip();
  uiToast("Trip reset", 0x2EE66B);
}
static void onReset(lv_event_t*) {
  ui_confirm("Reset trip?", "Trip distance, energy and max values will be cleared.", "Reset", doReset);
}

lv_obj_t* stats_create() {
  lv_obj_t* scr = ui_make_screen();
  lv_obj_t* c = ui_make_nav(scr, "TRIP STATS");
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(c, PAD, 0);
  lv_obj_set_style_pad_column(c, PAD, 0);

#if defined(UI_LARGE)
  const int cols = 4;
#else
  const int cols = 3;
#endif
  const int tileW = (SCREEN_W - 2 * PAD - (cols - 1) * PAD) / cols - 1;
  for (int i = 0; i < ST_COUNT; i++) {
    lv_obj_t* t = lv_obj_create(c);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(t, tileW, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(t, C_PANEL, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_radius(t, 6, 0);
    lv_obj_set_style_pad_all(t, PAD, 0);
    lv_obj_set_style_pad_row(t, 0, 0);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
    ui_label(t, F_TINY, C_DIM, kNames[i]);
    s_val[i] = ui_label(t, F_BODY, C_TEXT, "--");
  }

  lv_obj_t* b = ui_button(c, LV_SYMBOL_REFRESH "  Reset trip", onReset, nullptr);
  lv_obj_set_width(b, lv_pct(100));
  lv_obj_set_style_bg_color(b, lv_color_hex(0x5A1A1A), 0);
  return scr;
}

void stats_update(uint32_t) {
  const RideData& r = ride.data();
  const char* su = ui_speed_unit();
  const char* du = ui_dist_unit();
  char b[40];

  snprintf(b, sizeof(b), "%.2f %s", ui_dist(r.tripKm), du);
  ui_set_text(s_val[ST_TRIP], b);
  ui_fmt_duration(b, sizeof(b), r.rideSec);
  ui_set_text(s_val[ST_RIDE_TIME], b);
  snprintf(b, sizeof(b), "%.1f %s", ui_speed(r.avgSpeedKmh), su);
  ui_set_text(s_val[ST_AVG], b);
  snprintf(b, sizeof(b), "%.1f %s", ui_speed(r.maxSpeedKmh), su);
  ui_set_text(s_val[ST_MAX], b);

  snprintf(b, sizeof(b), "%.1f Wh", r.whUsed);
  ui_set_text(s_val[ST_WH_USED], b);
  snprintf(b, sizeof(b), "%.1f Wh", r.whRegen);
  ui_set_text(s_val[ST_WH_REGEN], b);
  if (r.whPerKm > 0) snprintf(b, sizeof(b), "%.1f Wh/%s", r.whPerKm / ui_dist(1.0f), du);
  else strcpy(b, "--");
  ui_set_text(s_val[ST_EFF], b);
  if (r.rangeKm >= 0) snprintf(b, sizeof(b), "%.0f %s", ui_dist(r.rangeKm), du);
  else strcpy(b, cfg.batteryAhX10 ? "--" : "set capacity");
  ui_set_text(s_val[ST_RANGE], b);

  snprintf(b, sizeof(b), "%.0f W", r.maxPowerW);
  ui_set_text(s_val[ST_MAX_PWR], b);
  snprintf(b, sizeof(b), "%.0f W", r.maxRegenW);
  ui_set_text(s_val[ST_MAX_REGEN], b);
  snprintf(b, sizeof(b), "%.1f A", r.maxCurrentIn);
  ui_set_text(s_val[ST_MAX_BATT_A], b);
  snprintf(b, sizeof(b), "%.1f A", r.maxCurrentMotor);
  ui_set_text(s_val[ST_MAX_PHASE_A], b);

  snprintf(b, sizeof(b), "%.1f V", r.minVoltage);
  ui_set_text(s_val[ST_MIN_V], b);
  snprintf(b, sizeof(b), "%.0f %s", ui_temp(r.maxTempFet), ui_temp_unit());
  ui_set_text(s_val[ST_MAX_FET], b);
  ui_set_color(s_val[ST_MAX_FET], ui_temp_color(r.maxTempFet, cfg.fetTempWarn, cfg.fetTempCrit));
  snprintf(b, sizeof(b), "%.0f %s", ui_temp(r.maxTempMotor), ui_temp_unit());
  ui_set_text(s_val[ST_MAX_MOTOR], b);
  ui_set_color(s_val[ST_MAX_MOTOR], ui_temp_color(r.maxTempMotor, cfg.motorTempWarn, cfg.motorTempCrit));
  snprintf(b, sizeof(b), "%.2f / %.2f", r.ahUsed, r.ahRegen);
  ui_set_text(s_val[ST_AH], b);

  snprintf(b, sizeof(b), "%.1f %s", ui_dist((float)r.odoKm), du);
  ui_set_text(s_val[ST_ODO], b);
  ui_fmt_duration(b, sizeof(b), r.sessionSec);
  ui_set_text(s_val[ST_SESSION], b);
}
