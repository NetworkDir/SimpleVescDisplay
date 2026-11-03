// Live charts of the last 60 seconds. History is recorded all the time,
// so the chart is already filled when the page is opened.
#include "ui_internal.h"
#include <math.h>
#include "../core/ride.h"
#include "../core/settings.h"

enum Channel : uint8_t { CH_SPEED_POWER, CH_CURRENT, CH_TEMP, CH_VOLT_DUTY, CH_COUNT };

struct ChannelInfo {
  const char* name[2];
  lv_color_t  color[2];
};

static int16_t   s_hist[CH_COUNT][2][CHART_POINTS];
static uint16_t  s_head = 0;    // next write position
static uint16_t  s_count = 0;
static uint8_t   s_channel = CH_SPEED_POWER;
static uint32_t  s_lastSample = 0;
static bool      s_newSample = false;

static lv_obj_t*          s_chart;
static lv_chart_series_t* s_ser[2];
static lv_obj_t*          s_legend[2];
static lv_obj_t*          s_axis[4];   // primary max/min, secondary max/min

static const char* kMap[] = {"SPD/PWR", "AMPS", "TEMP", "VOLT", ""};

static ChannelInfo info(uint8_t ch) {
  switch (ch) {
    case CH_CURRENT:   return {{"BATT A", "PHASE A"}, {C_GREEN, C_ORANGE}};
    case CH_TEMP:      return {{"ESC", "MOTOR"}, {C_YELLOW, C_RED}};
    case CH_VOLT_DUTY: return {{"VOLT", "DUTY %"}, {C_GREEN, C_PURPLE}};
    default:           return {{"SPEED", "POWER W"}, {C_ACCENT, C_YELLOW}};
  }
}

// integer sample of channel `ch`, series `s` (voltage is stored x10)
static int16_t sampleValue(uint8_t ch, int s, const RideData& r) {
  float v = 0;
  switch (ch) {
    case CH_SPEED_POWER: v = s == 0 ? ui_speed(r.speedKmh) : r.powerW; break;
    case CH_CURRENT:     v = s == 0 ? r.currentIn : r.currentMotor; break;
    case CH_TEMP:        v = ui_temp(s == 0 ? r.tempFet : r.tempMotor); break;
    case CH_VOLT_DUTY:   v = s == 0 ? r.voltage * 10.0f : r.dutyPct; break;
  }
  if (v > 32000) v = 32000;
  if (v < -32000) v = -32000;
  return (int16_t)lroundf(v);
}

static void fmtValue(char* b, size_t n, uint8_t ch, int s, int32_t v) {
  if (ch == CH_VOLT_DUTY && s == 0) snprintf(b, n, "%.1f", v / 10.0f);
  else snprintf(b, n, "%ld", (long)v);
}

static void updateRanges() {
  for (int s = 0; s < 2; s++) {
    int32_t lo = 0, hi = 0;
    for (uint16_t i = 0; i < s_count; i++) {
      int16_t v = s_hist[s_channel][s][i];
      if (i == 0 || v < lo) lo = v;
      if (i == 0 || v > hi) hi = v;
    }
    if (lo > 0 && !(s_channel == CH_VOLT_DUTY && s == 0)) lo = 0;  // keep zero visible
    int32_t span = hi - lo;
    if (span < 10) span = 10;
    hi += span / 10 + 1;
    if (lo < 0) lo -= span / 10 + 1;
    svd_chart_set_range(s_chart, s == 0 ? LV_CHART_AXIS_PRIMARY_Y : LV_CHART_AXIS_SECONDARY_Y, lo, hi);
    char b[16];
    fmtValue(b, sizeof(b), s_channel, s, hi);
    ui_set_text(s_axis[s * 2], b);
    fmtValue(b, sizeof(b), s_channel, s, lo);
    ui_set_text(s_axis[s * 2 + 1], b);
  }
}

static void refill() {
  const ChannelInfo ci = info(s_channel);
  for (int s = 0; s < 2; s++) {
    lv_chart_set_series_color(s_chart, s_ser[s], ci.color[s]);
    lv_obj_set_style_text_color(s_legend[s], ci.color[s], 0);
    lv_obj_set_style_text_color(s_axis[s * 2], ci.color[s], 0);
    lv_obj_set_style_text_color(s_axis[s * 2 + 1], ci.color[s], 0);
    // oldest -> newest; missing history as "no point"
    for (uint16_t i = 0; i < CHART_POINTS; i++) {
      if (i < CHART_POINTS - s_count) {
        lv_chart_set_next_value(s_chart, s_ser[s], LV_CHART_POINT_NONE);
      } else {
        uint16_t k = (s_head + CHART_POINTS - s_count + (i - (CHART_POINTS - s_count))) % CHART_POINTS;
        lv_chart_set_next_value(s_chart, s_ser[s], s_hist[s_channel][s][k]);
      }
    }
  }
  updateRanges();
}

static void onChannel(lv_event_t* e) {
  lv_obj_t* m = lv_event_get_target_obj(e);
  uint32_t id = lv_buttonmatrix_get_selected_button(m);
  if (id < CH_COUNT && id != s_channel) {
    s_channel = (uint8_t)id;
    refill();
    chart_update(millis());
  }
}

lv_obj_t* chart_create() {
  lv_obj_t* scr = ui_make_screen();
  lv_obj_t* c = ui_make_nav(scr, "CHARTS");
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(c, PAD, 0);

  lv_obj_t* m = lv_buttonmatrix_create(c);
  lv_buttonmatrix_set_map(m, kMap);
  lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CHECKABLE);
  lv_buttonmatrix_set_one_checked(m, true);
  lv_buttonmatrix_set_button_ctrl(m, s_channel, LV_BUTTONMATRIX_CTRL_CHECKED);
  lv_obj_set_size(m, lv_pct(100), BTN_H);
  lv_obj_set_style_pad_all(m, 2, 0);
  lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(m, 0, 0);
  lv_obj_set_style_text_font(m, F_SMALL, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(m, C_PANEL_HI, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(m, C_ACCENT, (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_add_event_cb(m, onChannel, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t* legend = lv_obj_create(c);
  lv_obj_remove_style_all(legend);
  lv_obj_set_size(legend, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  s_legend[0] = ui_label(legend, F_SMALL, C_ACCENT, "");
  s_legend[1] = ui_label(legend, F_SMALL, C_YELLOW, "");

  lv_obj_t* box = lv_obj_create(c);
  lv_obj_remove_style_all(box);
  lv_obj_set_width(box, lv_pct(100));
  lv_obj_set_flex_grow(box, 1);

  s_chart = lv_chart_create(box);
  lv_obj_set_size(s_chart, lv_pct(100), lv_pct(100));
  lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(s_chart, CHART_POINTS);
  lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_div_line_count(s_chart, 5, 7);
  lv_obj_set_style_bg_color(s_chart, C_PANEL, 0);
  lv_obj_set_style_border_color(s_chart, C_LINE, 0);
  lv_obj_set_style_line_color(s_chart, C_LINE, 0);
  lv_obj_set_style_radius(s_chart, 4, 0);
  lv_obj_set_style_pad_all(s_chart, 2, 0);
  lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
  lv_obj_set_style_width(s_chart, 0, LV_PART_INDICATOR);   // no point markers
  lv_obj_set_style_height(s_chart, 0, LV_PART_INDICATOR);
  lv_obj_remove_flag(s_chart, LV_OBJ_FLAG_CLICKABLE);

  s_ser[0] = lv_chart_add_series(s_chart, C_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
  s_ser[1] = lv_chart_add_series(s_chart, C_YELLOW, LV_CHART_AXIS_SECONDARY_Y);

  for (int i = 0; i < 4; i++) {
    s_axis[i] = ui_label(box, F_TINY, C_DIM, "");
    lv_obj_set_style_bg_color(s_axis[i], C_PANEL, 0);
    lv_obj_set_style_bg_opa(s_axis[i], LV_OPA_70, 0);
  }
  lv_obj_align(s_axis[0], LV_ALIGN_TOP_LEFT, 4, 2);
  lv_obj_align(s_axis[1], LV_ALIGN_BOTTOM_LEFT, 4, -2);
  lv_obj_align(s_axis[2], LV_ALIGN_TOP_RIGHT, -4, 2);
  lv_obj_align(s_axis[3], LV_ALIGN_BOTTOM_RIGHT, -4, -2);

  refill();
  return scr;
}

void chart_sample(uint32_t now) {
  if (now - s_lastSample < CHART_SAMPLE_MS) return;
  s_lastSample = now;
  const RideData& r = ride.data();
  for (uint8_t ch = 0; ch < CH_COUNT; ch++) {
    for (int s = 0; s < 2; s++) s_hist[ch][s][s_head] = r.connected ? sampleValue(ch, s, r) : 0;
  }
  s_head = (s_head + 1) % CHART_POINTS;
  if (s_count < CHART_POINTS) s_count++;
  s_newSample = true;
}

void chart_on_show() {  // chart_create() has just filled the chart from the history
  s_newSample = false;
  chart_update(millis());
}

void chart_update(uint32_t) {
  const ChannelInfo ci = info(s_channel);
  const uint16_t last = (s_head + CHART_POINTS - 1) % CHART_POINTS;
  if (s_newSample && s_count) {
    s_newSample = false;
    for (int s = 0; s < 2; s++) lv_chart_set_next_value(s_chart, s_ser[s], s_hist[s_channel][s][last]);
    updateRanges();
  }
  for (int s = 0; s < 2; s++) {
    char v[16], b[40];
    fmtValue(v, sizeof(v), s_channel, s, s_count ? s_hist[s_channel][s][last] : 0);
    const char* unit = "";
    if (s_channel == CH_SPEED_POWER && s == 0) unit = ui_speed_unit();
    if (s_channel == CH_TEMP) unit = ui_temp_unit();
    snprintf(b, sizeof(b), s == 0 ? LV_SYMBOL_STOP " %s %s %s" : "%s %s %s " LV_SYMBOL_STOP, ci.name[s], v, unit);
    ui_set_text(s_legend[s], b);
  }
}
