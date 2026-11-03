// Settings page. Built when opened and deleted when left (saves ~25 KB RAM).
// Values are written to `cfg` immediately and saved to flash when the page is left.
#include "ui_internal.h"
#include <math.h>
#include "ui.h"
#include "../core/settings.h"
#include "../core/ride.h"
#include "../core/sdlog.h"
#include "../vesc/telemetry.h"
#include "../ble/ble_bridge.h"
#include "../ble/ble_common.h"
#include "../hal/display.h"
#include "../hal/memguard.h"
#include "../core/ride_modes.h"

enum RowType : uint8_t { RT_NUM8, RT_NUM16, RT_INT8, RT_ENUM8, RT_SWITCH, RT_BAUD };
enum RowFlags : uint8_t { RF_NONE = 0, RF_RESTART = 1, RF_LIVE = 2 };

struct Row {
  RowType type;
  uint8_t flags;
  void* ptr;
  int32_t min, max, step, scale;
  const char* unit;
  const char* const* options;
  uint8_t optCount;
  int32_t special;            // value shown as `specialText` (-1 = none)
  const char* specialText;
  lv_obj_t* value;
  uint16_t repeat;
};

static const uint32_t kBauds[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
static const char* const kBaudNames[] = {"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"};
static const char* const kCommNames[] = {"UART", "CAN", "BLE"};
static const char* const kCanRates[] = {"125k", "250k", "500k", "1M"};
static const char* const kSpeedUnits[] = {"km/h", "mph"};
static const char* const kTempUnits[] = {"\xC2\xB0" "C", "\xC2\xB0" "F"};
static const char* const kBattTypes[] = {"Li-ion", "LiFePO4"};
static const char* const kStartPages[] = {"Dash", "Trip stats", "Live data", "Charts"};

static bool      s_restartNeeded = false;
static bool      s_scanWaiting = false;
static lv_obj_t* s_sdInfo = nullptr;
static lv_obj_t* s_bleInfo = nullptr;
static lv_obj_t* s_sysInfo = nullptr;
static lv_obj_t* s_odoInfo = nullptr;
static lv_obj_t* s_ldrInfo = nullptr;
static lv_obj_t* s_nameInfo = nullptr;
static lv_obj_t* s_modeInfo = nullptr;

// -----------------------------------------------------------------------------
//  Row value handling
// -----------------------------------------------------------------------------
static int32_t getVal(const Row* r) {
  switch (r->type) {
    case RT_NUM16: return *(uint16_t*)r->ptr;
    case RT_INT8:  return *(int8_t*)r->ptr;
    case RT_BAUD: {
      uint32_t b = *(uint32_t*)r->ptr;
      for (int i = 0; i < 8; i++) if (kBauds[i] == b) return i;
      return 4;
    }
    default: return *(uint8_t*)r->ptr;
  }
}

static void setVal(Row* r, int32_t v) {
  if (v < r->min) v = r->min;
  if (v > r->max) v = r->max;
  switch (r->type) {
    case RT_NUM16: *(uint16_t*)r->ptr = (uint16_t)v; break;
    case RT_INT8:  *(int8_t*)r->ptr = (int8_t)v; break;
    case RT_BAUD:  *(uint32_t*)r->ptr = kBauds[v]; break;
    default:       *(uint8_t*)r->ptr = (uint8_t)v; break;
  }
}

static void showVal(Row* r) {
  if (!r->value) return;
  char b[32];
  const int32_t v = getVal(r);
  if (r->special >= 0 && v == r->special) {
    snprintf(b, sizeof(b), "%s", r->specialText);
  } else if (r->type == RT_ENUM8 || r->type == RT_BAUD) {
    snprintf(b, sizeof(b), "%s", (v >= 0 && v < r->optCount) ? r->options[v] : "?");
  } else if (r->scale == 100) {
    snprintf(b, sizeof(b), "%.2f%s", v / 100.0f, r->unit);
  } else if (r->scale == 10) {
    snprintf(b, sizeof(b), "%.1f%s", v / 10.0f, r->unit);
  } else {
    snprintf(b, sizeof(b), (r->type == RT_INT8 && v > 0) ? "+%ld%s" : "%ld%s", (long)v, r->unit);
  }
  ui_set_text(r->value, b);
}

static void changed(Row* r) {
  settingsMarkDirty();
  if (r->flags & RF_RESTART) s_restartNeeded = true;
  if (r->ptr == &cfg.flip) display::applyRotation();
  if (r->ptr == &cfg.invertColors) display::applyInvert();
  if (r->flags & RF_LIVE) telemetry.settingsChanged();
}

static void rowDeleted(lv_event_t* e) { lv_free(lv_event_get_user_data(e)); }

// -----------------------------------------------------------------------------
//  Shared styles: a category has many rows, and local styles on every row and
//  button would cost ~150 bytes each.
// -----------------------------------------------------------------------------
static lv_style_t s_rowStyle, s_btnStyle, s_btnPressed;

static void initStyles() {
  static bool done = false;
  if (!done) {
    done = true;
    lv_style_init(&s_rowStyle);
    lv_style_set_width(&s_rowStyle, lv_pct(100));
    lv_style_set_height(&s_rowStyle, ROW_H);
    lv_style_set_border_side(&s_rowStyle, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_width(&s_rowStyle, 1);
    lv_style_set_pad_column(&s_rowStyle, PAD);
    lv_style_set_layout(&s_rowStyle, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&s_rowStyle, LV_FLEX_FLOW_ROW);
    lv_style_set_flex_main_place(&s_rowStyle, LV_FLEX_ALIGN_START);
    lv_style_set_flex_cross_place(&s_rowStyle, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_track_place(&s_rowStyle, LV_FLEX_ALIGN_CENTER);

    lv_style_init(&s_btnStyle);
    lv_style_set_width(&s_btnStyle, BTN_H + PAD);
    lv_style_set_height(&s_btnStyle, BTN_H - 4);
    lv_style_set_bg_color(&s_btnStyle, C_PANEL_HI);
    lv_style_set_shadow_width(&s_btnStyle, 0);
    lv_style_set_radius(&s_btnStyle, 6);
    lv_style_init(&s_btnPressed);
  }
  // colours follow the ride mode theme (no settings object exists at this point)
  lv_style_set_border_color(&s_rowStyle, C_LINE);
  lv_style_set_bg_color(&s_btnPressed, C_ACCENT);
}

// -----------------------------------------------------------------------------
//  Row widgets
// -----------------------------------------------------------------------------
static lv_obj_t* section(lv_obj_t* parent, const char* title) {
  lv_obj_t* l = ui_label(parent, F_BODY, C_ACCENT, title);
  lv_obj_set_style_pad_top(l, PAD * 2, 0);
  return l;
}

static lv_obj_t* rowBase(lv_obj_t* parent, const char* label) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_add_style(row, &s_rowStyle, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l = ui_label(row, F_SMALL, C_TEXT, label);
  lv_obj_set_flex_grow(l, 1);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  return row;
}

// Through LVGL's allocator (with the out-of-memory reserve); C++ `new` would abort().
static Row* newRow(lv_obj_t* rowObj, RowType type, void* ptr, uint8_t flags) {
  Row* r = (Row*)lv_malloc_zeroed(sizeof(Row));
  if (!r) return nullptr;
  r->type = type;
  r->flags = flags;
  r->ptr = ptr;
  r->min = 0;
  r->max = 255;
  r->step = 1;
  r->scale = 1;
  r->unit = "";
  r->options = nullptr;
  r->optCount = 0;
  r->special = -1;
  r->specialText = "";
  r->value = nullptr;
  r->repeat = 0;
  lv_obj_add_event_cb(rowObj, rowDeleted, LV_EVENT_DELETE, r);
  return r;
}

static lv_obj_t* smallButton(lv_obj_t* parent, const char* text) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_add_style(b, &s_btnStyle, 0);
  lv_obj_add_style(b, &s_btnPressed, LV_STATE_PRESSED);
  lv_obj_t* l = ui_label(b, F_BODY, C_TEXT, text);
  lv_obj_center(l);
  return b;
}

static void numStep(lv_event_t* e, int dir) {
  Row* r = (Row*)lv_event_get_user_data(e);
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    r->repeat = 0;
    return;
  }
  int32_t step = r->step;
  if (code == LV_EVENT_LONG_PRESSED_REPEAT) {
    r->repeat++;
    if (r->repeat > 25) step *= 50;      // accelerate when the button is held
    else if (r->repeat > 10) step *= 10;
  }
  setVal(r, getVal(r) + dir * step);
  showVal(r);
  changed(r);
}
static void numMinus(lv_event_t* e) { numStep(e, -1); }
static void numPlus(lv_event_t* e) { numStep(e, +1); }

static Row* numberRow(lv_obj_t* parent, const char* label, void* ptr, RowType type, int32_t min, int32_t max,
                      int32_t step, int32_t scale, const char* unit, uint8_t flags = RF_NONE,
                      int32_t special = -1, const char* specialText = "") {
  lv_obj_t* row = rowBase(parent, label);
  Row* r = newRow(row, type, ptr, flags);
  if (!r) return nullptr;
  r->min = min;
  r->max = max;
  r->step = step;
  r->scale = scale;
  r->unit = unit;
  r->special = special;
  r->specialText = specialText;

  lv_obj_t* minus = smallButton(row, LV_SYMBOL_MINUS);
  r->value = ui_label(row, F_SMALL, C_TEXT, "");
  lv_obj_set_width(r->value, SCREEN_W / 5);
  lv_obj_set_style_text_align(r->value, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t* plus = smallButton(row, LV_SYMBOL_PLUS);
  for (lv_event_code_t c : {LV_EVENT_SHORT_CLICKED, LV_EVENT_LONG_PRESSED_REPEAT, LV_EVENT_RELEASED}) {
    lv_obj_add_event_cb(minus, numMinus, c, r);
    lv_obj_add_event_cb(plus, numPlus, c, r);
  }
  showVal(r);
  return r;
}

static void enumTap(lv_event_t* e) {
  Row* r = (Row*)lv_event_get_user_data(e);
  int32_t v = getVal(r) + 1;
  if (v >= r->optCount) v = 0;
  setVal(r, v);
  showVal(r);
  changed(r);
}

static Row* enumRow(lv_obj_t* parent, const char* label, void* ptr, const char* const* opts, uint8_t count,
                    uint8_t flags = RF_NONE, RowType type = RT_ENUM8) {
  lv_obj_t* row = rowBase(parent, label);
  Row* r = newRow(row, type, ptr, flags);
  if (!r) return nullptr;
  r->options = opts;
  r->optCount = count;
  r->min = 0;
  r->max = count - 1;
  lv_obj_t* b = ui_button(row, "", enumTap, r);
  lv_obj_set_width(b, SCREEN_W * 3 / 10);
  lv_obj_set_height(b, BTN_H - 4);
  r->value = lv_obj_get_child(b, 0);
  showVal(r);
  return r;
}

static void switchChanged(lv_event_t* e) {
  Row* r = (Row*)lv_event_get_user_data(e);
  lv_obj_t* sw = lv_event_get_target_obj(e);
  *(uint8_t*)r->ptr = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0;
  changed(r);
}

static Row* switchRow(lv_obj_t* parent, const char* label, uint8_t* ptr, uint8_t flags = RF_NONE) {
  lv_obj_t* row = rowBase(parent, label);
  Row* r = newRow(row, RT_SWITCH, ptr, flags);
  if (!r) return nullptr;
  lv_obj_t* sw = lv_switch_create(row);
  lv_obj_set_size(sw, BTN_H * 3 / 2, BTN_H * 3 / 4);
  lv_obj_set_style_bg_color(sw, C_GREEN, (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (*ptr) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, switchChanged, LV_EVENT_VALUE_CHANGED, r);
  return r;
}

static lv_obj_t* buttonRow(lv_obj_t* parent, const char* label, const char* text, lv_event_cb_t cb,
                           lv_color_t color = C_PANEL_HI) {
  lv_obj_t* row = rowBase(parent, label);
  lv_obj_t* b = ui_button(row, text, cb, nullptr);
  lv_obj_set_height(b, BTN_H - 4);
  lv_obj_set_style_bg_color(b, color, 0);
  return b;
}

static lv_obj_t* infoRow(lv_obj_t* parent, const char* label, const char* text) {
  lv_obj_t* row = rowBase(parent, label);
  lv_obj_t* v = ui_label(row, F_SMALL, C_DIM, text);
  lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
  return v;
}

static void brightnessChanged(lv_event_t* e) {
  cfg.brightness = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(e));
  settingsMarkDirty();
}

static void sliderRow(lv_obj_t* parent, const char* label) {
  lv_obj_t* row = rowBase(parent, label);
  lv_obj_t* s = lv_slider_create(row);
  lv_obj_set_width(s, SCREEN_W * 2 / 5);
  lv_slider_set_range(s, 10, 255);
  lv_slider_set_value(s, cfg.brightness, LV_ANIM_OFF);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_GESTURE_BUBBLE);  // dragging must not switch pages
  lv_obj_set_style_pad_hor(s, PAD * 2, 0);
  lv_obj_add_event_cb(s, brightnessChanged, LV_EVENT_VALUE_CHANGED, nullptr);
}

// -----------------------------------------------------------------------------
//  Actions
// -----------------------------------------------------------------------------
static void restartNow() {
  settingsSave();
  ride.saveOdo();
  delay(100);
  ESP.restart();
}

static void onScan(lv_event_t*) {
  if (cfg.demoMode || cfg.commMode == COMM_BLE) {
    ui_info("CAN scan", "Available in UART mode (via the VESC) or CAN mode.");
    return;
  }
  telemetry.startScan();
  s_scanWaiting = true;
  uiToast("Scanning CAN bus...", 0x00B4FF);
}

static void onCalibrate(lv_event_t*) { display::calibrateTouch(); }

static void doResetTrip() {
  ride.resetTrip();
  uiToast("Trip reset", 0x2EE66B);
}
static void onResetTrip(lv_event_t*) {
  ui_confirm("Reset trip?", "Trip distance, energy and max values will be cleared.", "Reset", doResetTrip);
}

static void doForgetBle() {
  bleBridge.forgetBonds();
  cfg.bleAddrValid = 0;
  restartNow();
}
static void onForgetBle(lv_event_t*) {
  ui_confirm("Forget Bluetooth?", "Removes paired phones and the bound VESC BLE module, then restarts.", "Forget",
             doForgetBle);
}

static void onRestart(lv_event_t*) { restartNow(); }

static void doFactory() {
  settingsFactoryReset();
  restartNow();
}
static void onFactory(lv_event_t*) {
  ui_confirm("Factory reset?", "All settings return to defaults. The odometer is kept.", "Reset", doFactory);
}

// ---- odometer keypad ----------------------------------------------------------
static lv_obj_t* s_kpBox = nullptr;
static lv_obj_t* s_kpLabel = nullptr;
static char      s_kpBuf[8];

static void kpClose() {
  if (s_kpBox) lv_obj_delete_async(s_kpBox);
  s_kpBox = nullptr;
}

static void kpEvent(lv_event_t* e) {
  lv_obj_t* m = lv_event_get_target_obj(e);
  const char* t = lv_buttonmatrix_get_button_text(m, lv_buttonmatrix_get_selected_button(m));
  if (!t) return;
  size_t n = strlen(s_kpBuf);
  if (strcmp(t, LV_SYMBOL_BACKSPACE) == 0) {
    if (n) s_kpBuf[n - 1] = 0;
  } else if (strcmp(t, LV_SYMBOL_CLOSE) == 0) {
    kpClose();
    return;
  } else if (strcmp(t, LV_SYMBOL_OK) == 0) {
    const float v = (float)atol(s_kpBuf);
    ride.setOdo(cfg.speedUnit ? v / 0.621371f : v);
    uiToast("Odometer updated", 0x2EE66B);
    kpClose();
    return;
  } else if (n < 6) {
    s_kpBuf[n] = t[0];
    s_kpBuf[n + 1] = 0;
  }
  char b[24];
  snprintf(b, sizeof(b), "%s %s", s_kpBuf[0] ? s_kpBuf : "0", ui_dist_unit());
  lv_label_set_text(s_kpLabel, b);
}

static void onSetOdo(lv_event_t*) {
  static const char* map[] = {"1", "2", "3", LV_SYMBOL_BACKSPACE, "\n",
                                    "4", "5", "6", LV_SYMBOL_CLOSE, "\n",
                                    "7", "8", "9", "\n",
                                    "0", LV_SYMBOL_OK, ""};
  if (s_kpBox || !ui_dialog_allowed()) return;  // a second tap would orphan the first keypad
  snprintf(s_kpBuf, sizeof(s_kpBuf), "%ld", (long)lroundf(ui_dist((float)ride.data().odoKm)));
  s_kpBox = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_kpBox, SCREEN_W * 9 / 10, SCREEN_H * 9 / 10);
  lv_obj_center(s_kpBox);
  lv_obj_set_style_bg_color(s_kpBox, C_PANEL, 0);
  lv_obj_set_style_border_color(s_kpBox, C_ACCENT, 0);
  lv_obj_set_style_border_width(s_kpBox, 2, 0);
  lv_obj_set_style_pad_all(s_kpBox, PAD, 0);
  lv_obj_set_flex_flow(s_kpBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(s_kpBox, LV_OBJ_FLAG_SCROLLABLE);
  ui_label(s_kpBox, F_SMALL, C_DIM, "SET ODOMETER");
  s_kpLabel = ui_label(s_kpBox, F_VALUE, C_TEXT, "");
  char b[24];
  snprintf(b, sizeof(b), "%s %s", s_kpBuf, ui_dist_unit());
  lv_label_set_text(s_kpLabel, b);
  lv_obj_t* m = lv_buttonmatrix_create(s_kpBox);
  lv_buttonmatrix_set_map(m, map);
  lv_obj_set_width(m, lv_pct(100));
  lv_obj_set_flex_grow(m, 1);
  lv_obj_set_style_text_font(m, F_BODY, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(m, C_PANEL_HI, LV_PART_ITEMS);
  lv_obj_add_event_cb(m, kpEvent, LV_EVENT_VALUE_CHANGED, nullptr);
}

// ---- Bluetooth name (on-screen keyboard) ---------------------------------------
static lv_obj_t* s_kbdBox = nullptr;
static lv_obj_t* s_kbdTa = nullptr;

static void kbdClose() {
  if (s_kbdBox) lv_obj_delete_async(s_kbdBox);
  s_kbdBox = nullptr;
}

static void kbdEvent(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    const char* t = lv_textarea_get_text(s_kbdTa);
    size_t n = 0;
    for (; t[n] && n < sizeof(cfg.bleName) - 1; n++) cfg.bleName[n] = t[n];
    cfg.bleName[n] = 0;
    settingsMarkDirty();
    s_restartNeeded = true;
    if (s_nameInfo) ui_set_text(s_nameInfo, ble::deviceName());
    uiToast("Name saved - applied after restart", 0x2EE66B);
    kbdClose();
  } else if (code == LV_EVENT_CANCEL) {
    kbdClose();
  }
}

void settings_close_dialogs() {
  kpClose();
  kbdClose();
}

static void onRename(lv_event_t*) {
  if (s_kbdBox || !ui_dialog_allowed()) return;
  s_kbdBox = lv_obj_create(lv_layer_top());
  lv_obj_set_pos(s_kbdBox, 0, 0);
  lv_obj_set_size(s_kbdBox, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(s_kbdBox, C_BG, 0);
  lv_obj_set_style_radius(s_kbdBox, 0, 0);
  lv_obj_set_style_border_width(s_kbdBox, 0, 0);
  lv_obj_set_style_pad_all(s_kbdBox, PAD, 0);
  lv_obj_set_style_pad_row(s_kbdBox, PAD, 0);
  lv_obj_set_flex_flow(s_kbdBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(s_kbdBox, LV_OBJ_FLAG_SCROLLABLE);

  ui_label(s_kbdBox, F_SMALL, C_DIM, "Bluetooth name (empty = default).  " LV_SYMBOL_OK " saves");
  s_kbdTa = lv_textarea_create(s_kbdBox);
  lv_textarea_set_one_line(s_kbdTa, true);
  lv_textarea_set_max_length(s_kbdTa, sizeof(cfg.bleName) - 1);
  lv_textarea_set_text(s_kbdTa, cfg.bleName);
  lv_obj_set_width(s_kbdTa, lv_pct(100));
  lv_obj_set_style_text_font(s_kbdTa, F_BODY, 0);

  lv_obj_t* kb = lv_keyboard_create(s_kbdBox);
  lv_obj_set_width(kb, lv_pct(100));
  lv_obj_set_flex_grow(kb, 1);
  lv_obj_set_style_text_font(kb, F_SMALL, LV_PART_ITEMS);
  lv_keyboard_set_textarea(kb, s_kbdTa);
  lv_obj_add_event_cb(kb, kbdEvent, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(kb, kbdEvent, LV_EVENT_CANCEL, nullptr);
}

// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//  Page structure: a menu of categories; one category is built at a time
//  (keeps RAM usage low - the full list would need ~80 KB).
// -----------------------------------------------------------------------------
enum Category : int8_t {
  CAT_MENU = -1, CAT_MODES, CAT_CONN, CAT_BLE, CAT_VEHICLE, CAT_DASH, CAT_ALERTS, CAT_DISPLAY, CAT_DATA, CAT_SYSTEM,
  CAT_COUNT
};
static const char* const kCatNames[CAT_COUNT] = {
  LV_SYMBOL_CHARGE "  Ride modes",
  LV_SYMBOL_SHUFFLE "  Connection", LV_SYMBOL_BLUETOOTH "  Bluetooth", LV_SYMBOL_GPS "  Vehicle & battery",
  LV_SYMBOL_HOME "  Dashboard", LV_SYMBOL_BELL "  Units & warnings", LV_SYMBOL_IMAGE "  Display",
  LV_SYMBOL_SD_CARD "  Data & trip", LV_SYMBOL_SETTINGS "  System",
};

static lv_obj_t* s_content = nullptr;
static int8_t    s_cat = CAT_MENU;

static void clearInfoPointers() {
  s_sdInfo = s_bleInfo = s_sysInfo = s_odoInfo = s_ldrInfo = s_nameInfo = s_modeInfo = nullptr;
}

static void buildPage(lv_obj_t* c, int8_t cat);

static uint8_t   s_modeEdit = MODE_ECO;   // ride mode shown in the Ride modes category
static bool      s_keepModeView = false;  // rebuilt by "Edit mode": scroll back to it
static lv_obj_t* s_scrollTo = nullptr;

static void showCategoryAsync(void* p) {
  if (!s_content) return;  // settings page was closed meanwhile
  clearInfoPointers();
  lv_obj_clean(s_content);
  lv_obj_scroll_to_y(s_content, 0, LV_ANIM_OFF);
  s_cat = (int8_t)(intptr_t)p;
  if (s_cat != CAT_MENU && !memguard::roomForPage()) {  // the small menu always fits
    uiToast("Not enough memory - try again", 0xFFC81E);
    s_cat = CAT_MENU;
  }
  s_scrollTo = nullptr;
  buildPage(s_content, s_cat);
  if (s_keepModeView && s_scrollTo) {
    lv_obj_update_layout(s_content);
    lv_obj_scroll_to_view(s_scrollTo, LV_ANIM_OFF);
  }
  s_keepModeView = false;
  s_scrollTo = nullptr;
  settings_update(millis());
}

static void onModeEdit(lv_event_t*) {
  s_modeEdit = (s_modeEdit + 1) % MODE_COUNT;
  s_keepModeView = true;
  lv_async_call(showCategoryAsync, (void*)(intptr_t)CAT_MODES);
}

// The rows that trigger this are deleted by the rebuild -> do it outside the event.
static void onCategory(lv_event_t* e) { lv_async_call(showCategoryAsync, lv_event_get_user_data(e)); }

static void menuButton(lv_obj_t* c, const char* text, intptr_t cat) {
  lv_obj_t* b = ui_button(c, text, onCategory, (void*)cat);
  lv_obj_set_width(b, lv_pct(100));
  lv_obj_set_height(b, ROW_H - 4);
  lv_obj_t* l = lv_obj_get_child(b, 0);
  lv_obj_set_style_text_font(l, F_BODY, 0);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  if (cat != CAT_MENU) {
    lv_obj_t* arrow = ui_label(b, F_BODY, C_DIM, LV_SYMBOL_RIGHT);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);
  }
}

static void buildPage(lv_obj_t* c, int8_t cat) {
  char b[48];
  if (cat == CAT_MENU) {
    lv_obj_set_style_pad_row(c, PAD, 0);
    for (int i = 0; i < CAT_COUNT; i++) menuButton(c, kCatNames[i], i);
    return;
  }
  lv_obj_set_style_pad_row(c, 0, 0);
  menuButton(c, LV_SYMBOL_LEFT "  Back", CAT_MENU);
  section(c, kCatNames[cat]);

  switch (cat) {
    case CAT_CONN:
      enumRow(c, "Interface", &cfg.commMode, kCommNames, 3, RF_RESTART);
      enumRow(c, "UART baud", &cfg.uartBaud, kBaudNames, 8, RF_RESTART, RT_BAUD);
      enumRow(c, "CAN bitrate", &cfg.canBitrate, kCanRates, 4, RF_RESTART);
      numberRow(c, "VESC CAN ID", &cfg.canVescId, RT_NUM8, 0, 255, 1, 1, "", RF_LIVE, 255, "AUTO");
      numberRow(c, "Display CAN ID", &cfg.canOwnId, RT_NUM8, 0, 254, 1, 1, "", RF_RESTART);
      numberRow(c, "2nd VESC ID", &cfg.secondVescId, RT_NUM8, 0, 255, 1, 1, "", RF_LIVE, 255, "OFF");
      numberRow(c, "Poll rate", &cfg.pollHz, RT_NUM8, 2, 25, 1, 1, " Hz", RF_LIVE);
      buttonRow(c, "Find VESCs on CAN", LV_SYMBOL_REFRESH " Scan", onScan);
      break;

    case CAT_BLE:
      switchRow(c, "VESC Tool bridge", &cfg.bleBridge, RF_RESTART);
      switchRow(c, "Bridge needs PIN", &cfg.blePairing, RF_RESTART);
      snprintf(b, sizeof(b), "%03lu %03lu", (unsigned long)(cfg.blePasskey / 1000),
               (unsigned long)(cfg.blePasskey % 1000));
      infoRow(c, "Pairing PIN", b);
      s_nameInfo = infoRow(c, "Device name", ble::deviceName());
      buttonRow(c, "Bluetooth name", LV_SYMBOL_EDIT " Rename", onRename);
      s_bleInfo = infoRow(c, "Status", "");
      buttonRow(c, "Paired devices", LV_SYMBOL_TRASH " Forget", onForgetBle);
      break;

    case CAT_VEHICLE:
      numberRow(c, "Motor poles", &cfg.motorPoles, RT_NUM8, 2, 120, 2, 1, "");
      numberRow(c, "Wheel diameter", &cfg.wheelDiameterMm, RT_NUM16, 50, 1500, 1, 1, " mm");
      numberRow(c, "Gear ratio", &cfg.gearRatioX100, RT_NUM16, 10, 5000, 1, 100, "");
      numberRow(c, "Speed correction", &cfg.speedCorrPct, RT_INT8, -20, 20, 1, 1, " %");
      enumRow(c, "Battery type", &cfg.battType, kBattTypes, 2);
      numberRow(c, "Battery cells", &cfg.batteryCells, RT_NUM8, 3, 30, 1, 1, " S");
      numberRow(c, "Battery capacity", &cfg.batteryAhX10, RT_NUM16, 0, 5000, 5, 10, " Ah");
      break;

    case CAT_DASH: {
      static const char* const cellLabels[5] = {"Cell 1 (left)", "Cell 2", "Cell 3 (middle)", "Cell 4",
                                                "Cell 5 (right)"};
      for (int i = 0; i < 5; i++) enumRow(c, cellLabels[i], &cfg.dashCell[i], kDashCellNames, CELL_COUNT);
      infoRow(c, "Tip", "long press a cell on the dash");
      switchRow(c, "Speed with decimal", &cfg.speedDecimals);
      numberRow(c, "Power bar full scale", &cfg.powerScaleW, RT_NUM16, 0, 30000, 250, 1, " W", RF_NONE, 0, "AUTO");
      switchRow(c, "Gauge sweep at start", &cfg.gaugeSweep);
      enumRow(c, "Start page", &cfg.startPage, kStartPages, 4);
      break;
    }

    case CAT_ALERTS:
      enumRow(c, "Speed unit", &cfg.speedUnit, kSpeedUnits, 2);
      enumRow(c, "Temperature unit", &cfg.tempUnit, kTempUnits, 2);
      numberRow(c, "Speed warning", &cfg.warnSpeed, RT_NUM8, 5, 200, 1, 1, " km/h");
      numberRow(c, "ESC temp warn", &cfg.fetTempWarn, RT_NUM8, 30, 120, 1, 1, " \xC2\xB0" "C");
      numberRow(c, "ESC temp critical", &cfg.fetTempCrit, RT_NUM8, 30, 130, 1, 1, " \xC2\xB0" "C");
      numberRow(c, "Motor temp warn", &cfg.motorTempWarn, RT_NUM8, 30, 160, 1, 1, " \xC2\xB0" "C");
      numberRow(c, "Motor temp critical", &cfg.motorTempCrit, RT_NUM8, 30, 180, 1, 1, " \xC2\xB0" "C");
      numberRow(c, "Duty warning", &cfg.warnDutyPct, RT_NUM8, 50, 100, 1, 1, " %");
      numberRow(c, "Low battery", &cfg.warnBattPct, RT_NUM8, 0, 50, 1, 1, " %");
      switchRow(c, "Beeper", &cfg.buzzer);
      switchRow(c, "Speed warning beep", &cfg.speedBeep);
      switchRow(c, "Touch click sound", &cfg.touchSound);
      switchRow(c, "Status LED (RGB)", &cfg.led);
      break;

    case CAT_DISPLAY:
      switchRow(c, "Auto brightness (LDR)", &cfg.autoBrightness);
      sliderRow(c, "Brightness");
      numberRow(c, "Auto: minimum level", &cfg.autoMinBright, RT_NUM8, 5, 255, 5, 1, "");
      numberRow(c, "Auto: dark at sensor", &cfg.ldrDark, RT_NUM16, 300, 4095, 50, 1, "");
      s_ldrInfo = infoRow(c, "Light sensor now", "");
      numberRow(c, "Dim when idle", &cfg.dimMinutes, RT_NUM8, 0, 60, 1, 1, " min", RF_NONE, 0, "OFF");
      switchRow(c, "Rotate 180\xC2\xB0", &cfg.flip);
      switchRow(c, "Invert colours", &cfg.invertColors);
      switchRow(c, "Swap red/blue", &cfg.swapRB, RF_RESTART);
      switchRow(c, "Back to dash while riding", &cfg.autoReturn);
      switchRow(c, "Boot logo", &cfg.bootLogo);
      infoRow(c, "Own logo", "SD: /splash.png or .jpg");
      buttonRow(c, "Touch screen", "Calibrate", onCalibrate);
      break;

    case CAT_DATA:
      switchRow(c, "Keep trip after restart", &cfg.keepTrip);
      buttonRow(c, "Trip", LV_SYMBOL_REFRESH " Reset", onResetTrip, lv_color_hex(0x5A1A1A));
      s_odoInfo = infoRow(c, "Odometer", "");
      buttonRow(c, "Change odometer", LV_SYMBOL_EDIT " Set", onSetOdo);
      switchRow(c, "SD card logging", &cfg.sdLog);
      s_sdInfo = infoRow(c, "SD card", "");
      switchRow(c, "Demo mode (no VESC)", &cfg.demoMode, RF_RESTART);
      break;

    case CAT_MODES: {
      switchRow(c, "Ride modes (VESC limits)", &cfg.rideModes);
      s_modeInfo = infoRow(c, "Status", "");
      switchRow(c, "Theme colour follows mode", &cfg.modeTheme);
      infoRow(c, "Change mode", "tap the top centre of the dash");
      {
        // one mode at a time: all four at once were ~30 rows (too much RAM next to Bluetooth)
        const uint8_t m = s_modeEdit;
        lv_obj_t* eb = buttonRow(c, "Edit mode (tap = next)", rideModeName(m), onModeEdit,
                                lv_color_hex(rideModeColor(m)));
        lv_obj_set_width(eb, SCREEN_W * 3 / 10);
        lv_obj_set_style_text_color(lv_obj_get_child(eb, 0), C_BG, 0);
        s_scrollTo = lv_obj_get_parent(eb);
        numberRow(c, "Motor current", &cfg.modeCurrent[m], RT_NUM8, 10, 100, 5, 1, " %");
        numberRow(c, "Regen / brake", &cfg.modeBrake[m], RT_NUM8, 10, 100, 5, 1, " %");
        numberRow(c, "Speed limit", &cfg.modeSpeed[m], RT_NUM8, 0, 150, 1, 1, " km/h", RF_NONE, 0, "VESC");
        switchRow(c, "Traction control", &cfg.modeTcs[m]);
      }
      section(c, "TRACTION CONTROL");
      switchRow(c, "Switch TCS with the mode", &cfg.tcsControl);
      infoRow(c, "Needs", "VESC FW 7.00.2+, dual motor");
      const vesc::FwInfo& fw = telemetry.fw();
      if (fw.valid) snprintf(b, sizeof(b), "FW %u.%02u: %s", fw.major, fw.minor, rideModeTcsSupported() ? "supported" : "not supported");
      else snprintf(b, sizeof(b), "VESC firmware unknown");
      infoRow(c, "Your VESC", b);
      infoRow(c, "Note", "limits are temporary (VESC restart)");
      break;
    }

    case CAT_SYSTEM:
      snprintf(b, sizeof(b), "%s v%s", FW_NAME, FW_VERSION);
      infoRow(c, "Firmware", b);
      infoRow(c, "Board", BOARD_NAME);
      snprintf(b, sizeof(b), "LVGL %d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
      infoRow(c, "Graphics", b);
      s_sysInfo = infoRow(c, "Free memory", "");
      infoRow(c, "Last restart", memguard::resetReasonText());
      buttonRow(c, "Restart display", LV_SYMBOL_POWER " Restart", onRestart);
      buttonRow(c, "All settings", "Factory reset", onFactory, lv_color_hex(0x5A1A1A));
      break;

    default:
      break;
  }
}

lv_obj_t* settings_create() {
  initStyles();
  lv_obj_t* scr = ui_make_screen();
  s_content = ui_make_nav(scr, LV_SYMBOL_SETTINGS " SETTINGS");
  lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
  s_restartNeeded = false;
  clearInfoPointers();
  s_cat = CAT_MENU;
  buildPage(s_content, s_cat);
  return scr;
}

void settings_update(uint32_t now) {
  char b[48];
  if (s_sdInfo) {
    switch (sdlogState()) {
      case SdState::OFF:     strcpy(b, "logging off"); break;
      case SdState::NO_CARD: strcpy(b, "no card"); break;
      case SdState::READY:   strcpy(b, "ready (waiting for data)"); break;
      case SdState::LOGGING: snprintf(b, sizeof(b), "%s", sdlogFile() + 5); break;
      default:               strcpy(b, "error"); break;
    }
    ui_set_text(s_sdInfo, b);
  }
  if (s_bleInfo) {
    if (cfg.commMode == COMM_BLE) snprintf(b, sizeof(b), "link mode");
    else if (!bleBridge.active()) snprintf(b, sizeof(b), "off");
    else snprintf(b, sizeof(b), "%s", bleBridge.connected() ? "VESC Tool connected" : "waiting");
    ui_set_text(s_bleInfo, b);
  }
  if (s_odoInfo) {
    snprintf(b, sizeof(b), "%.1f %s", ui_dist((float)ride.data().odoKm), ui_dist_unit());
    ui_set_text(s_odoInfo, b);
  }
  if (s_ldrInfo) {
    snprintf(b, sizeof(b), "%u  (dark >= %u)", display::ldrRaw(), cfg.ldrDark);
    ui_set_text(s_ldrInfo, b);
  }
  if (s_modeInfo) {
    snprintf(b, sizeof(b), "%s: %s", rideModeName(cfg.rideMode), rideModeStateText());
    ui_set_text(s_modeInfo, b);
  }
  if (s_sysInfo) {
    snprintf(b, sizeof(b), "%u KB (min %u KB)", (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned)(ESP.getMinFreeHeap() / 1024));
    ui_set_text(s_sysInfo, b);
  }

  if (s_scanWaiting && telemetry.scanState() == ScanState::DONE) {
    s_scanWaiting = false;
    char msg[96];
    if (telemetry.scanCount() == 0) {
      snprintf(msg, sizeof(msg), "No VESC found on the CAN bus.");
    } else {
      int n = snprintf(msg, sizeof(msg), "Found CAN IDs:");
      for (uint8_t i = 0; i < telemetry.scanCount() && n < (int)sizeof(msg) - 6; i++)
        n += snprintf(msg + n, sizeof(msg) - n, " %u", telemetry.scanIds()[i]);
    }
    telemetry.clearScan();
    ui_info("CAN scan", msg);
  }
  (void)now;
}

void settings_on_leave() {
  settingsSave();
  rideModesParamsChanged();  // edited mode limits are applied when standing still
  telemetry.settingsChanged();
  clearInfoPointers();
  s_content = nullptr;
  s_scanWaiting = false;
  if (s_restartNeeded) {
    s_restartNeeded = false;
    ui_confirm("Restart needed", "Some changes are applied after a restart.", "Restart", restartNow);
  }
}
