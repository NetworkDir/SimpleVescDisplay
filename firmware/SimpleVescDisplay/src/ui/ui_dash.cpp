// Main dashboard: big speed, battery, power, duty & power bars, 5 configurable cells.
// Touch actions:
//   tap battery  -> % / volts          tap power -> watts / amps
//   tap speed    -> max+avg / range+efficiency / odometer+ride time
//   long press a cell -> choose what it shows (+ "reset trip" on the trip cell)
//   tap the centre notch -> ride mode chooser (statistics page when ride modes are off)
//   swipe left/right -> other pages
#include "ui_internal.h"
#include <math.h>
#include "ui.h"
#include "../core/ride.h"
#include "../core/alerts.h"
#include "../core/settings.h"
#include "../core/sdlog.h"
#include "../vesc/telemetry.h"
#include "../ble/ble_bridge.h"
#include "../ble/ble_link.h"
#include "../core/ride_modes.h"

static constexpr int W = SCREEN_W;
static constexpr int H = SCREEN_H;
static constexpr int CENTER_TOP = HDR_H;
static constexpr int CENTER_BOT = H - FTR_H;
static constexpr int CENTER_H = CENTER_BOT - CENTER_TOP;

static lv_obj_t *s_speed, *s_unit, *s_sub;
static lv_obj_t *s_battIcon, *s_battMain, *s_battSub;
static lv_obj_t *s_pwrMain, *s_pwrSub;
static lv_obj_t *s_notch, *s_mode, *s_link, *s_icons, *s_alert;
static lv_obj_t *s_barPwr, *s_barDuty, *s_barPwrVal, *s_barDutyVal;
static lv_obj_t *s_cellVal[5], *s_cellCap[5];

static uint8_t s_battMode = 0;  // 0 = percent big, 1 = volts big
static uint8_t s_pwrMode = 0;   // 0 = watts, 1 = battery amps
static uint8_t s_subMode = 0;
static float   s_speedFilt = 0;
static uint32_t s_sweepStart = 0;   // start-up gauge sweep, 0 = finished

// 0 -> 99 -> 0 "hello" sweep (like a car dashboard), -1 when finished
static int sweepValue(uint32_t now) {
  if (!s_sweepStart) return -1;
  const uint32_t t = now - s_sweepStart;
  if (t < 1100) {
    const float f = t / 1100.0f;
    return (int)(99.0f * (1.0f - (1.0f - f) * (1.0f - f)));  // ease out
  }
  if (t < 1500) return 99;
  if (t < 2600) {
    const float f = (t - 1500) / 1100.0f;
    return (int)(99.0f * (1.0f - f * f));  // ease in
  }
  s_sweepStart = 0;
  return -1;
}
static int     s_editCell = -1;
static lv_obj_t* s_chooser = nullptr;

const char* const kDashCellNames[CELL_COUNT] = {
  "ESC temp", "Motor temp", "Trip", "Odometer", "Battery A", "Phase A", "Power", "Voltage", "Battery %",
  "Duty", "Range", "Efficiency", "Wh used", "Ah used", "Ride time", "Avg speed", "Max speed",
};
// button matrix map for the chooser: 3 per row
// not "const char* const": LVGL 9.1 takes a non-const array
static const char* kChooserMap[] = {
  "ESC temp", "Motor temp", "Trip", "\n",
  "Odometer", "Battery A", "Phase A", "\n",
  "Power", "Voltage", "Battery %", "\n",
  "Duty", "Range", "Efficiency", "\n",
  "Wh used", "Ah used", "Ride time", "\n",
  "Avg speed", "Max speed", "",
};

// road decoration (static: lv_line keeps a pointer to the points)
static lv_point_precise_t s_roadL[2], s_roadR[2], s_dash[3][2];

static void setIndicatorColor(lv_obj_t* bar, lv_color_t c) {
  if (!lv_color_eq(lv_obj_get_style_bg_color(bar, LV_PART_INDICATOR), c))
    lv_obj_set_style_bg_color(bar, c, LV_PART_INDICATOR);
}

static lv_obj_t* plainObj(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static lv_obj_t* roadLine(lv_obj_t* parent, lv_point_precise_t* pts, int width) {
  lv_obj_t* l = lv_line_create(parent);
  lv_line_set_points(l, pts, 2);
  lv_obj_set_style_line_color(l, C_ROAD, 0);
  lv_obj_set_style_line_width(l, width, 0);
  lv_obj_set_style_line_rounded(l, true, 0);
  lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
  return l;
}

// ---- touch handlers -----------------------------------------------------------
static void onBattTap(lv_event_t*) { s_battMode ^= 1; }
static void onPwrTap(lv_event_t*) { s_pwrMode ^= 1; }
static void onSpeedTap(lv_event_t*) { s_subMode = (s_subMode + 1) % 3; }
// ---- ride mode chooser -----------------------------------------------------------
static lv_obj_t* s_modeBox = nullptr;

static void closeModeBox() {
  if (s_modeBox) lv_obj_delete_async(s_modeBox);
  s_modeBox = nullptr;
}
static void onModeClose(lv_event_t*) { closeModeBox(); }
static void onModePick(lv_event_t* e) {
  const char* why = nullptr;
  if (rideModeSelect((uint8_t)(intptr_t)lv_event_get_user_data(e), &why)) closeModeBox();
  else uiToast(why, 0xFFC81E);
}

void dash_open_mode_chooser() {
  if (s_modeBox || !ui_dialog_allowed()) return;
  s_modeBox = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_modeBox, SCREEN_W - 2 * PAD, SCREEN_H - 2 * PAD);
  lv_obj_center(s_modeBox);
  lv_obj_set_style_bg_color(s_modeBox, C_PANEL, 0);
  lv_obj_set_style_border_color(s_modeBox, C_ACCENT, 0);
  lv_obj_set_style_border_width(s_modeBox, 2, 0);
  lv_obj_set_style_pad_all(s_modeBox, PAD, 0);
  lv_obj_set_style_pad_gap(s_modeBox, PAD, 0);
  lv_obj_set_flex_flow(s_modeBox, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_remove_flag(s_modeBox, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* top = plainObj(s_modeBox);
  lv_obj_set_size(top, lv_pct(100), BTN_H);
  lv_obj_t* title = ui_label(top, F_BODY, C_TEXT, "RIDE MODE");
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* close = ui_button(top, LV_SYMBOL_CLOSE, onModeClose, nullptr);
  lv_obj_align(close, LV_ALIGN_RIGHT_MID, 0, 0);

  const int bw = (SCREEN_W - 4 * PAD) / 2 - PAD;
  const int bh = (SCREEN_H - 4 * PAD - BTN_H) / 2 - PAD;
  for (uint8_t m = 0; m < MODE_COUNT; m++) {
    const lv_color_t col = lv_color_hex(rideModeColor(m));
    const bool sel = m == cfg.rideMode;
    lv_obj_t* b = lv_button_create(s_modeBox);
    lv_obj_set_size(b, bw, bh);
    lv_obj_set_style_bg_color(b, sel ? col : lv_color_mix(col, C_BG, 70), 0);
    lv_obj_set_style_border_color(b, sel ? C_TEXT : col, 0);
    lv_obj_set_style_border_width(b, sel ? 3 : 1, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(b, onModePick, LV_EVENT_CLICKED, (void*)(intptr_t)m);
    ui_label(b, F_LARGE, sel ? C_BG : C_TEXT, rideModeName(m));
    char sum[40];
    int n = snprintf(sum, sizeof(sum), "%u%% power", cfg.modeCurrent[m]);
    if (cfg.modeSpeed[m]) n += snprintf(sum + n, sizeof(sum) - n, "  %d %s", (int)lroundf(ui_speed(cfg.modeSpeed[m])), ui_speed_unit());
    if (cfg.tcsControl && !cfg.modeTcs[m]) snprintf(sum + n, sizeof(sum) - n, "  TCS off");
    ui_label(b, F_SMALL, sel ? C_BG : C_DIM, sum);
  }
}

static void onNotchTap(lv_event_t*) {
  if (cfg.rideModes) dash_open_mode_chooser();
  else ui_goto(SCR_STATS);
}
static void doResetTrip() {
  ride.resetTrip();
  uiToast("Trip reset", 0x2EE66B);
}

static void closeChooser() {
  if (s_chooser) lv_obj_delete_async(s_chooser);
  s_chooser = nullptr;
  s_editCell = -1;
}
static void onChooserPick(lv_event_t* e) {
  uint32_t id = lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
  if (s_editCell >= 0 && id < CELL_COUNT) {
    cfg.dashCell[s_editCell] = (uint8_t)id;
    settingsMarkDirty();
  }
  closeChooser();
}
static void onChooserClose(lv_event_t*) { closeChooser(); }

void dash_close_dialogs() {
  closeChooser();
  closeModeBox();
}
static void onChooserReset(lv_event_t*) {
  closeChooser();
  ui_confirm("Reset trip?", "Trip distance, energy and max values will be cleared.", "Reset", doResetTrip);
}

static void onCellLong(lv_event_t* e) {
  lv_indev_wait_release(lv_indev_active());  // no click after the long press
  if (s_chooser || !ui_dialog_allowed()) return;
  s_editCell = (int)(intptr_t)lv_event_get_user_data(e);

  s_chooser = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_chooser, SCREEN_W - 2 * PAD, SCREEN_H - 2 * PAD);
  lv_obj_center(s_chooser);
  lv_obj_set_style_bg_color(s_chooser, C_PANEL, 0);
  lv_obj_set_style_border_color(s_chooser, C_ACCENT, 0);
  lv_obj_set_style_border_width(s_chooser, 2, 0);
  lv_obj_set_style_pad_all(s_chooser, PAD, 0);
  lv_obj_set_style_pad_row(s_chooser, PAD, 0);
  lv_obj_set_flex_flow(s_chooser, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(s_chooser, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* top = plainObj(s_chooser);
  lv_obj_set_size(top, lv_pct(100), BTN_H);
  ui_label(top, F_SMALL, C_ACCENT, "Show in this cell:");
  lv_obj_t* close = ui_button(top, LV_SYMBOL_CLOSE, onChooserClose, nullptr);
  lv_obj_align(close, LV_ALIGN_RIGHT_MID, 0, 0);
  if (cfg.dashCell[s_editCell] == CELL_TRIP) {
    lv_obj_t* r = ui_button(top, LV_SYMBOL_REFRESH " Reset trip", onChooserReset, nullptr);
    lv_obj_set_style_bg_color(r, lv_color_hex(0x5A1A1A), 0);
    lv_obj_align_to(r, close, LV_ALIGN_OUT_LEFT_MID, -PAD, 0);
  }
  lv_obj_t* first = lv_obj_get_child(top, 0);
  lv_obj_align(first, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t* m = lv_buttonmatrix_create(s_chooser);
  lv_buttonmatrix_set_map(m, kChooserMap);
  lv_obj_set_width(m, lv_pct(100));
  lv_obj_set_flex_grow(m, 1);
  lv_obj_set_style_pad_all(m, 2, 0);
  lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(m, 0, 0);
  lv_obj_set_style_text_font(m, F_SMALL, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(m, C_PANEL_HI, LV_PART_ITEMS);
  lv_buttonmatrix_set_button_ctrl(m, cfg.dashCell[s_editCell], LV_BUTTONMATRIX_CTRL_CHECKED);
  lv_obj_set_style_bg_color(m, C_ACCENT, (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_add_event_cb(m, onChooserPick, LV_EVENT_VALUE_CHANGED, nullptr);
}

// ---- building blocks ------------------------------------------------------------
static lv_obj_t* makeBar(lv_obj_t* parent, int x, bool symmetrical) {
  const int labelH = lv_font_get_line_height(F_TINY) + 2;
  lv_obj_t* bar = lv_bar_create(parent);
  lv_obj_set_size(bar, BAR_W, CENTER_H - 2 * (labelH + PAD * 2));
  lv_obj_set_pos(bar, x, CENTER_TOP + PAD * 2 + labelH);
  lv_obj_set_style_radius(bar, 3, 0);
  lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(bar, C_PANEL, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(bar, C_LINE, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_anim_duration(bar, 0, 0);
  if (symmetrical) {
    lv_bar_set_mode(bar, LV_BAR_MODE_SYMMETRICAL);
    lv_bar_set_range(bar, -100, 100);
  } else {
    lv_bar_set_range(bar, 0, 100);
  }
  lv_bar_set_value(bar, 0, LV_ANIM_OFF);
  return bar;
}

static lv_obj_t* barCaption(lv_obj_t* parent, lv_obj_t* bar, const char* text, bool top) {
  lv_obj_t* l = ui_label(parent, F_TINY, top ? C_TEXT : C_DIM, text);
  lv_obj_align_to(l, bar, top ? LV_ALIGN_OUT_TOP_MID : LV_ALIGN_OUT_BOTTOM_MID, 0, top ? -2 : 2);
  return l;
}

lv_obj_t* dash_create() {
  lv_obj_t* scr = ui_make_screen();
  static bool firstCreate = true;  // the dash is rebuilt when the theme changes: sweep only at boot
  s_sweepStart = (firstCreate && cfg.gaugeSweep) ? millis() : 0;
  firstCreate = false;

  // ---------- road perspective (behind everything else) ----------
  const int vanishY = CENTER_TOP + CENTER_H * 3 / 10;
  s_roadL[0] = {(lv_value_precise_t)(W * 36 / 100), (lv_value_precise_t)CENTER_BOT};
  s_roadL[1] = {(lv_value_precise_t)(W * 45 / 100), (lv_value_precise_t)vanishY};
  s_roadR[0] = {(lv_value_precise_t)(W * 64 / 100), (lv_value_precise_t)CENTER_BOT};
  s_roadR[1] = {(lv_value_precise_t)(W * 55 / 100), (lv_value_precise_t)vanishY};
  roadLine(scr, s_roadL, 3);
  roadLine(scr, s_roadR, 3);
  const int segH = (CENTER_BOT - vanishY) / 7;
  for (int i = 0; i < 3; i++) {
    const int y0 = CENTER_BOT - segH * (i * 2) - segH / 2;
    s_dash[i][0] = {(lv_value_precise_t)(W / 2), (lv_value_precise_t)y0};
    s_dash[i][1] = {(lv_value_precise_t)(W / 2), (lv_value_precise_t)(y0 - segH * 7 / 10)};
    roadLine(scr, s_dash[i], 2);
  }

  // ---------- header ----------
  lv_obj_t* hdr = plainObj(scr);
  lv_obj_set_pos(hdr, 0, 0);
  lv_obj_set_size(hdr, W, HDR_H);
  lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(hdr, C_PANEL_HI, 0);
  lv_obj_set_style_bg_grad_color(hdr, C_PANEL, 0);
  lv_obj_set_style_bg_grad_dir(hdr, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(hdr, C_LINE, 0);
  lv_obj_set_style_border_width(hdr, 2, 0);

  // battery (left)
  lv_obj_t* batt = plainObj(hdr);
  lv_obj_set_size(batt, W * 3 / 10, HDR_H);
  lv_obj_set_pos(batt, 0, 0);
  lv_obj_add_flag(batt, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(batt, onBattTap, LV_EVENT_CLICKED, nullptr);
  s_battIcon = ui_label(batt, F_VALUE, C_GREEN, LV_SYMBOL_BATTERY_FULL);
  lv_obj_align(s_battIcon, LV_ALIGN_LEFT_MID, PAD, 0);
  s_battMain = ui_label(batt, F_VALUE, C_TEXT, "--");
  lv_obj_align_to(s_battMain, s_battIcon, LV_ALIGN_OUT_RIGHT_TOP, PAD, -lv_font_get_line_height(F_TINY) / 2);
  s_battSub = ui_label(batt, F_TINY, C_DIM, "");
  lv_obj_align_to(s_battSub, s_battMain, LV_ALIGN_OUT_BOTTOM_LEFT, 0, -2);

  // power (right)
  lv_obj_t* pwr = plainObj(hdr);
  lv_obj_set_size(pwr, W * 3 / 10, HDR_H);
  lv_obj_align(pwr, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_add_flag(pwr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(pwr, onPwrTap, LV_EVENT_CLICKED, nullptr);
  s_pwrMain = ui_label(pwr, F_VALUE, C_TEXT, "0");
  lv_obj_align(s_pwrMain, LV_ALIGN_TOP_RIGHT, -PAD, (HDR_H - lv_font_get_line_height(F_VALUE) -
                                                     lv_font_get_line_height(F_TINY)) / 2);
  s_pwrSub = ui_label(pwr, F_TINY, C_DIM, "WATTS");
  lv_obj_align_to(s_pwrSub, s_pwrMain, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, -2);

  // centre notch: rounded only at the bottom (top corners are pushed off-screen)
  s_notch = lv_obj_create(scr);
  lv_obj_remove_flag(s_notch, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_notch, W * 36 / 100, HDR_H + HDR_H / 2);
  lv_obj_align(s_notch, LV_ALIGN_TOP_MID, 0, -HDR_H / 2 + 2);
  lv_obj_set_style_radius(s_notch, HDR_H / 2, 0);
  lv_obj_set_style_bg_color(s_notch, C_BG, 0);
  lv_obj_set_style_border_color(s_notch, C_LINE, 0);
  lv_obj_set_style_border_width(s_notch, 2, 0);
  lv_obj_set_style_pad_all(s_notch, 0, 0);
  lv_obj_add_event_cb(s_notch, onNotchTap, LV_EVENT_CLICKED, nullptr);
  const int notchInner = HDR_H / 2;  // visible part starts here
  // first line: [ride mode] [link] [bluetooth / SD icons], centred
  lv_obj_t* top = plainObj(s_notch);
  lv_obj_remove_flag(top, LV_OBJ_FLAG_CLICKABLE);  // taps go to the notch
  lv_obj_set_size(top, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(top, PAD, 0);
  lv_obj_align(top, LV_ALIGN_TOP_MID, 0, notchInner);
  s_mode = ui_label(top, F_SMALL, C_ACCENT, "");
  s_link = ui_label(top, F_TINY, C_DIM, "UART");
  s_icons = ui_label(top, F_TINY, C_ACCENT, "");
  if (!cfg.rideModes) lv_obj_add_flag(s_mode, LV_OBJ_FLAG_HIDDEN);
  s_alert = ui_label(s_notch, F_SMALL, C_YELLOW, "");
  lv_obj_set_width(s_alert, W * 34 / 100 - 2 * PAD);
  lv_obj_set_style_text_align(s_alert, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s_alert, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align(s_alert, LV_ALIGN_TOP_MID, 0, notchInner + lv_font_get_line_height(F_SMALL));

  // ---------- speed ----------
  const int speedH = lv_font_get_line_height(F_SPEED);
  const int unitH = lv_font_get_line_height(F_BODY);
  const int subH = lv_font_get_line_height(F_SMALL);
  const int blockTop = CENTER_TOP + (CENTER_H - speedH - unitH - subH) / 2;

  s_speed = ui_label(scr, F_SPEED, C_TEXT, "--");
  lv_obj_set_width(s_speed, W - 2 * (BAR_W + PAD * 6));
  lv_obj_set_style_text_align(s_speed, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_speed, LV_ALIGN_TOP_MID, 0, blockTop);
  lv_obj_add_flag(s_speed, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_speed, onSpeedTap, LV_EVENT_CLICKED, nullptr);

  s_unit = ui_label(scr, F_BODY, C_DIM, "km/h");
  lv_obj_align(s_unit, LV_ALIGN_TOP_MID, 0, blockTop + speedH + 2);
  s_sub = ui_label(scr, F_SMALL, C_DIM, "");
  lv_obj_set_width(s_sub, W - 2 * (BAR_W + PAD * 6));
  lv_obj_set_style_text_align(s_sub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_sub, LV_ALIGN_TOP_MID, 0, blockTop + speedH + unitH + 2);

  // ---------- side bars ----------
  s_barPwr = makeBar(scr, PAD * 2, true);
  s_barPwrVal = barCaption(scr, s_barPwr, "0", true);
  barCaption(scr, s_barPwr, "PWR", false);
  s_barDuty = makeBar(scr, W - PAD * 2 - BAR_W, false);
  s_barDutyVal = barCaption(scr, s_barDuty, "0%", true);
  barCaption(scr, s_barDuty, "DUTY", false);

  // ---------- footer ----------
  lv_obj_t* ftr = plainObj(scr);
  lv_obj_set_pos(ftr, 0, CENTER_BOT);
  lv_obj_set_size(ftr, W, FTR_H);
  lv_obj_set_style_bg_opa(ftr, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(ftr, C_PANEL, 0);
  lv_obj_set_style_bg_grad_color(ftr, C_BG, 0);
  lv_obj_set_style_bg_grad_dir(ftr, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_side(ftr, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(ftr, C_LINE, 0);
  lv_obj_set_style_border_width(ftr, 2, 0);
  lv_obj_set_flex_flow(ftr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ftr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  for (int i = 0; i < 5; i++) {
    lv_obj_t* cell = plainObj(ftr);
    lv_obj_set_size(cell, W / 5, FTR_H - 4);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_cellVal[i] = ui_label(cell, F_VALUE, C_GREEN, "--");
    s_cellCap[i] = ui_label(cell, F_TINY, C_DIM, "");
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cell, onCellLong, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)i);
  }
  return scr;
}

// Value, caption and colour of one configurable footer cell.
static void cellContent(uint8_t type, const RideData& r, char* v, size_t vn, char* cap, size_t cn, lv_color_t& col) {
  const bool c = r.connected;
  col = C_TEXT;
  switch (type) {
    case CELL_ESC_TEMP:
    case CELL_MOTOR_TEMP: {
      const bool esc = type == CELL_ESC_TEMP;
      const float t = esc ? r.tempFet : r.tempMotor;
      snprintf(cap, cn, "%s %s", esc ? "ESC" : "MOTOR", ui_temp_unit());
      if (c) snprintf(v, vn, "%d\xC2\xB0", (int)lroundf(ui_temp(t)));
      col = esc ? ui_temp_color(t, cfg.fetTempWarn, cfg.fetTempCrit) : ui_temp_color(t, cfg.motorTempWarn, cfg.motorTempCrit);
      break;
    }
    case CELL_TRIP: {
      const float t = ui_dist(r.tripKm);
      snprintf(v, vn, t < 100 ? "%.1f" : "%.0f", t);
      snprintf(cap, cn, "TRIP %s", ui_dist_unit());
      return;
    }
    case CELL_ODO:
      snprintf(v, vn, "%.0f", ui_dist((float)r.odoKm));
      snprintf(cap, cn, "ODO %s", ui_dist_unit());
      return;
    case CELL_BATT_A:
    case CELL_PHASE_A: {
      const float a = type == CELL_BATT_A ? r.currentIn : r.currentMotor;
      snprintf(cap, cn, type == CELL_BATT_A ? "BATT A" : "PHASE A");
      if (c) snprintf(v, vn, "%d", (int)lroundf(a));
      col = a < -0.5f ? C_REGEN : C_GREEN;
      break;
    }
    case CELL_POWER:
      snprintf(cap, cn, "POWER W");
      if (c) {
        if (fabsf(r.powerW) >= 10000) snprintf(v, vn, "%.1fk", r.powerW / 1000.0f);
        else snprintf(v, vn, "%d", (int)lroundf(r.powerW));
      }
      col = r.powerW < -1 ? C_REGEN : C_TEXT;
      break;
    case CELL_VOLTAGE:
      snprintf(cap, cn, "VOLT");
      if (c) snprintf(v, vn, "%.1f", r.voltage);
      break;
    case CELL_BATT_PCT:
      snprintf(cap, cn, "BATT %%");
      if (c && r.battPct >= 0) snprintf(v, vn, "%d", (int)lroundf(r.battPct));
      col = r.battPct <= cfg.warnBattPct ? C_YELLOW : C_GREEN;
      break;
    case CELL_DUTY:
      snprintf(cap, cn, "DUTY %%");
      if (c) snprintf(v, vn, "%d", (int)lroundf(r.dutyPct));
      col = r.dutyPct >= cfg.warnDutyPct ? C_RED : r.dutyPct >= cfg.warnDutyPct - 15 ? C_YELLOW : C_GREEN;
      break;
    case CELL_RANGE:
      snprintf(cap, cn, "RANGE %s", ui_dist_unit());
      if (r.rangeKm >= 0) snprintf(v, vn, "%d", (int)lroundf(ui_dist(r.rangeKm)));
      return;
    case CELL_EFFICIENCY:
      snprintf(cap, cn, "Wh/%s", ui_dist_unit());
      if (r.whPerKm > 0) snprintf(v, vn, "%.1f", r.whPerKm / ui_dist(1.0f));
      return;
    case CELL_WH_USED:
      snprintf(cap, cn, "Wh USED");
      snprintf(v, vn, "%.0f", r.whUsed - r.whRegen);
      return;
    case CELL_AH_USED:
      snprintf(cap, cn, "Ah USED");
      snprintf(v, vn, "%.2f", r.ahUsed - r.ahRegen);
      return;
    case CELL_RIDE_TIME:
      snprintf(cap, cn, "RIDE TIME");
      ui_fmt_duration(v, vn, r.rideSec);
      return;
    case CELL_AVG_SPEED:
    case CELL_MAX_SPEED:
      snprintf(cap, cn, "%s %s", type == CELL_AVG_SPEED ? "AVG" : "MAX", ui_speed_unit());
      snprintf(v, vn, "%d", (int)lroundf(ui_speed(type == CELL_AVG_SPEED ? r.avgSpeedKmh : r.maxSpeedKmh)));
      return;
    default:
      break;
  }
  if (!c) col = C_DIM;  // live values without a VESC link: v stays "--"
}

// ---- updates ------------------------------------------------------------------
static const char* battSymbol(float pct) {
  if (pct > 80) return LV_SYMBOL_BATTERY_FULL;
  if (pct > 55) return LV_SYMBOL_BATTERY_3;
  if (pct > 30) return LV_SYMBOL_BATTERY_2;
  if (pct > 10) return LV_SYMBOL_BATTERY_1;
  return LV_SYMBOL_BATTERY_EMPTY;
}

void dash_update(uint32_t now) {
  const RideData& r = ride.data();
  const AlertState& a = alerts();
  const bool conn = r.connected;
  char buf[48], buf2[24];

  // ---- speed ----
  s_speedFilt = conn ? s_speedFilt * 0.4f + r.speedKmh * 0.6f : 0;
  if (conn && cfg.speedDecimals) snprintf(buf, sizeof(buf), "%.1f", ui_speed(s_speedFilt));
  else if (conn) snprintf(buf, sizeof(buf), "%d", (int)lroundf(ui_speed(s_speedFilt)));
  else strcpy(buf, "--");
  ui_set_text(s_speed, buf);
  ui_set_color(s_speed, !conn ? C_DIM : (a.active & AL_SPEED) ? C_RED : C_TEXT);
  ui_set_text(s_unit, ui_speed_unit());

  switch (s_subMode) {
    case 0:
      snprintf(buf, sizeof(buf), "MAX %d   AVG %d", (int)lroundf(ui_speed(r.maxSpeedKmh)),
               (int)lroundf(ui_speed(r.avgSpeedKmh)));
      break;
    case 1:
      if (r.rangeKm >= 0) snprintf(buf2, sizeof(buf2), "%d %s", (int)lroundf(ui_dist(r.rangeKm)), ui_dist_unit());
      else strcpy(buf2, "--");
      if (r.whPerKm > 0)
        snprintf(buf, sizeof(buf), "RANGE %s   %.1f Wh/%s", buf2, r.whPerKm / ui_dist(1.0f), ui_dist_unit());
      else snprintf(buf, sizeof(buf), "RANGE %s", buf2);
      break;
    default:
      ui_fmt_duration(buf2, sizeof(buf2), r.rideSec);
      snprintf(buf, sizeof(buf), "ODO %.0f %s   RIDE %s", ui_dist((float)r.odoKm), ui_dist_unit(), buf2);
      break;
  }
  ui_set_text(s_sub, buf);

  // ---- battery ----
  const bool haveBatt = conn && r.battPct >= 0;
  ui_set_text(s_battIcon, haveBatt ? battSymbol(r.battPct) : LV_SYMBOL_BATTERY_EMPTY);
  lv_color_t bc = !haveBatt ? C_DIM
                  : (a.active & (AL_BATT_EMPTY | AL_BATT_HIGH)) ? C_RED
                  : (a.active & AL_BATT_LOW) ? C_YELLOW : C_GREEN;
  ui_set_color(s_battIcon, bc);
  if (haveBatt) {
    char pct[12], volt[12];
    snprintf(pct, sizeof(pct), "%d%%", (int)lroundf(r.battPct));
    snprintf(volt, sizeof(volt), "%.1fV", r.voltage);
    ui_set_text(s_battMain, s_battMode ? volt : pct);
    ui_set_text(s_battSub, s_battMode ? pct : volt);
  } else {
    ui_set_text(s_battMain, "--");
    ui_set_text(s_battSub, "BATTERY");
  }
  ui_set_color(s_battMain, haveBatt ? bc : C_DIM);

  // ---- power ----
  if (s_pwrMode == 0) {
    const float p = r.powerW;
    if (fabsf(p) >= 10000) snprintf(buf, sizeof(buf), "%.1fk", p / 1000.0f);
    else snprintf(buf, sizeof(buf), "%d", (int)lroundf(p));
    ui_set_text(s_pwrSub, p < -1 ? "REGEN W" : "WATTS");
  } else {
    snprintf(buf, sizeof(buf), "%.1f", r.currentIn);
    ui_set_text(s_pwrSub, "BATT A");
  }
  ui_set_text(s_pwrMain, conn ? buf : "--");
  ui_set_color(s_pwrMain, !conn ? C_DIM : r.powerW < -1 ? C_REGEN : C_TEXT);

  // ---- bars ----
  const float scale = cfg.powerScaleW ? (float)cfg.powerScaleW : fmaxf(500.0f, fmaxf(r.maxPowerW, r.maxRegenW));
  int pv = (int)(r.powerW / scale * 100.0f);
  pv = pv > 100 ? 100 : pv < -100 ? -100 : pv;
  lv_bar_set_value(s_barPwr, conn ? pv : 0, LV_ANIM_OFF);
  lv_color_t pc = pv < 0 ? C_REGEN : pv < 50 ? C_GREEN : pv < 80 ? C_YELLOW : C_RED;
  setIndicatorColor(s_barPwr, pc);
  if (fabsf(r.powerW) >= 1000) snprintf(buf, sizeof(buf), "%.1fk", r.powerW / 1000.0f);
  else snprintf(buf, sizeof(buf), "%d", (int)r.powerW);
  ui_set_text(s_barPwrVal, conn ? buf : "-");

  const int duty = conn ? (int)lroundf(r.dutyPct) : 0;
  lv_bar_set_value(s_barDuty, duty, LV_ANIM_OFF);
  lv_color_t dc = duty >= cfg.warnDutyPct ? C_RED : duty >= cfg.warnDutyPct - 15 ? C_YELLOW : C_GREEN;
  setIndicatorColor(s_barDuty, dc);
  snprintf(buf, sizeof(buf), "%d%%", duty);
  ui_set_text(s_barDutyVal, buf);
  ui_set_color(s_barDutyVal, duty >= cfg.warnDutyPct ? C_RED : C_TEXT);

  // ---- notch: link + status icons + alert ----
  const char* linkName = cfg.demoMode ? "DEMO" : telemetry.linkName();
  if (cfg.commMode == COMM_BLE && !cfg.demoMode && !conn) {
    snprintf(buf, sizeof(buf), "BLE %s", BleLink::stateName());
    ui_set_text(s_link, buf);
  } else {
    ui_set_text(s_link, linkName);
  }
  ui_set_color(s_link, cfg.demoMode ? C_PURPLE : conn ? C_GREEN : C_RED);
  buf[0] = 0;
  if (bleBridge.connected()) strcat(buf, LV_SYMBOL_BLUETOOTH " ");
  if (sdlogState() == SdState::LOGGING) strcat(buf, LV_SYMBOL_SD_CARD);
  ui_set_text(s_icons, buf);

  // ride mode badge: solid when active, blinking while it is being applied
  const bool showMode = cfg.rideModes != 0;
  if (showMode == lv_obj_has_flag(s_mode, LV_OBJ_FLAG_HIDDEN)) {
    if (showMode) lv_obj_remove_flag(s_mode, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_mode, LV_OBJ_FLAG_HIDDEN);
  }
  if (showMode) {
    const ModeState ms = rideModeState();
    lv_color_t mc = lv_color_hex(rideModeColor(cfg.rideMode));
    if (ms == ModeState::FAILED) mc = C_RED;
    else if (ms != ModeState::ACTIVE && ((now / 400) & 1)) mc = C_DIM;
    ui_set_text(s_mode, rideModeName(cfg.rideMode));
    ui_set_color(s_mode, mc);
  }

  ui_set_text(s_alert, a.text);
  ui_set_color(s_alert, a.level == AlertLevel::CRIT ? C_RED : a.level == AlertLevel::WARN ? C_YELLOW : C_DIM);
  const bool blinkOn = a.level == AlertLevel::CRIT && ((now / 400) & 1);
  lv_color_t border = blinkOn ? C_RED : C_LINE;
  if (!lv_color_eq(lv_obj_get_style_border_color(s_notch, LV_PART_MAIN), border)) lv_obj_set_style_border_color(s_notch, border, 0);

  // ---- start-up sweep overrides speed and bars for ~2.6 s ----
  const int sweep = sweepValue(now);
  if (sweep >= 0) {
    snprintf(buf, sizeof(buf), "%d", sweep);
    ui_set_text(s_speed, buf);
    ui_set_color(s_speed, C_ACCENT);
    lv_bar_set_value(s_barPwr, sweep, LV_ANIM_OFF);
    setIndicatorColor(s_barPwr, sweep < 50 ? C_GREEN : sweep < 80 ? C_YELLOW : C_RED);
    lv_bar_set_value(s_barDuty, sweep, LV_ANIM_OFF);
    setIndicatorColor(s_barDuty, sweep < 50 ? C_GREEN : sweep < 80 ? C_YELLOW : C_RED);
    snprintf(buf, sizeof(buf), "%d%%", sweep);
    ui_set_text(s_barDutyVal, buf);
  }

  // ---- footer: 5 configurable cells ----
  for (int i = 0; i < 5; i++) {
    lv_color_t col;
    strcpy(buf, "--");
    buf2[0] = 0;
    cellContent(cfg.dashCell[i], r, buf, sizeof(buf), buf2, sizeof(buf2), col);
    ui_set_text(s_cellVal[i], buf);
    ui_set_color(s_cellVal[i], col);
    ui_set_text(s_cellCap[i], buf2);
  }
}
