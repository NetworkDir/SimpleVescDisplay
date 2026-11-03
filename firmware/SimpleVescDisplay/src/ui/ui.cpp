#include "ui.h"
#include "ui_internal.h"
#include <string.h>
#include <esp_heap_caps.h>
#include "../core/settings.h"
#include "../core/ride.h"
#include "../ble/ble_bridge.h"
#include "../hal/display.h"
#include "../hal/memguard.h"
#include "../core/ride_modes.h"

static lv_obj_t*  s_screens[SCR_COUNT] = {nullptr};
static ScreenId   s_current = SCR_DASH;
static lv_obj_t*  s_toast = nullptr;
static lv_timer_t* s_toastTimer = nullptr;
static lv_obj_t*  s_pinBox = nullptr;
static lv_obj_t*  s_pinLabel = nullptr;

// -----------------------------------------------------------------------------
//  Units & formatting
// -----------------------------------------------------------------------------
float ui_speed(float kmh) { return cfg.speedUnit ? kmh * 0.621371f : kmh; }
float ui_dist(float km) { return cfg.speedUnit ? km * 0.621371f : km; }
const char* ui_speed_unit() { return cfg.speedUnit ? "mph" : "km/h"; }
const char* ui_dist_unit() { return cfg.speedUnit ? "mi" : "km"; }
float ui_temp(float c) { return cfg.tempUnit ? c * 1.8f + 32.0f : c; }
const char* ui_temp_unit() { return cfg.tempUnit ? "\xC2\xB0" "F" : "\xC2\xB0" "C"; }

lv_color_t ui_temp_color(float c, int warn, int crit) {
  if (c >= crit) return C_RED;
  if (c >= warn) return C_YELLOW;
  return C_GREEN;
}

void ui_fmt_duration(char* buf, size_t n, uint32_t sec) {
  if (sec >= 3600) snprintf(buf, n, "%lu:%02lu:%02lu", (unsigned long)(sec / 3600), (unsigned long)(sec / 60 % 60),
                            (unsigned long)(sec % 60));
  else snprintf(buf, n, "%lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
}

// -----------------------------------------------------------------------------
//  Widgets
// -----------------------------------------------------------------------------
void ui_set_text(lv_obj_t* label, const char* text) {
  const char* cur = lv_label_get_text(label);
  if (!cur || strcmp(cur, text) != 0) lv_label_set_text(label, text);
}

void ui_set_color(lv_obj_t* obj, lv_color_t c) {
  if (!lv_color_eq(lv_obj_get_style_text_color(obj, LV_PART_MAIN), c)) lv_obj_set_style_text_color(obj, c, 0);
}

lv_obj_t* ui_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, text);
  return l;
}

lv_obj_t* ui_button(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* user) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_height(b, BTN_H);
  lv_obj_set_style_bg_color(b, C_PANEL_HI, 0);
  lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_hor(b, PAD * 2, 0);
  lv_obj_t* l = ui_label(b, F_SMALL, C_TEXT, text);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  return b;
}

// -----------------------------------------------------------------------------
//  Screens & navigation
// -----------------------------------------------------------------------------
static void gestureCb(lv_event_t*) {
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
  if (dir == LV_DIR_LEFT) ui_goto((ScreenId)((s_current + 1) % SCR_COUNT));
  else if (dir == LV_DIR_RIGHT) ui_goto((ScreenId)((s_current + SCR_COUNT - 1) % SCR_COUNT));
}

lv_obj_t* ui_make_screen() {
  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(scr, C_TEXT, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, gestureCb, LV_EVENT_GESTURE, nullptr);
  return scr;
}

static void navCb(lv_event_t* e) {
  const intptr_t dir = (intptr_t)lv_event_get_user_data(e);
  if (dir == 0) ui_goto(SCR_DASH);
  else ui_goto((ScreenId)((s_current + SCR_COUNT + dir) % SCR_COUNT));
}

static lv_obj_t* navButton(lv_obj_t* parent, const char* sym, intptr_t dir) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, NAV_H + PAD * 2, NAV_H - 6);
  lv_obj_set_style_bg_color(b, C_PANEL_HI, 0);
  lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_t* l = ui_label(b, F_BODY, C_TEXT, sym);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, navCb, LV_EVENT_CLICKED, (void*)dir);
  return b;
}

lv_obj_t* ui_make_nav(lv_obj_t* scr, const char* title) {
  lv_obj_t* bar = lv_obj_create(scr);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_size(bar, SCREEN_W, NAV_H);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, C_PANEL, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(bar, C_LINE, 0);
  lv_obj_set_style_border_width(bar, 2, 0);
  lv_obj_set_style_pad_all(bar, 3, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* prev = navButton(bar, LV_SYMBOL_LEFT, -1);
  lv_obj_align(prev, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* next = navButton(bar, LV_SYMBOL_RIGHT, 1);
  lv_obj_align(next, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t* home = lv_button_create(bar);
  lv_obj_set_height(home, NAV_H - 6);
  lv_obj_set_style_bg_opa(home, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(home, C_PANEL_HI, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(home, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(home, 0, 0);
  lv_obj_align(home, LV_ALIGN_CENTER, 0, 0);
  lv_obj_t* t = ui_label(home, F_BODY, C_TEXT, title);
  lv_obj_center(t);
  lv_obj_add_event_cb(home, navCb, LV_EVENT_CLICKED, (void*)0);

  lv_obj_t* content = lv_obj_create(scr);
  lv_obj_set_pos(content, 0, NAV_H);
  lv_obj_set_size(content, SCREEN_W, SCREEN_H - NAV_H);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_radius(content, 0, 0);
  lv_obj_set_style_pad_all(content, PAD, 0);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_ACTIVE);
  return content;
}

ScreenId ui_current() { return s_current; }

// Only the dashboard lives permanently; every other page is built when opened and
// deleted when left. This keeps ~60 KB of RAM free for BLE, SD and dialogs.
static lv_obj_t* createPage(ScreenId id) {
  switch (id) {
    case SCR_STATS:    return stats_create();
    case SCR_LIVE:     return live_create();
    case SCR_CHART:    return chart_create();
    case SCR_SETTINGS: return settings_create();
    default:           return nullptr;
  }
}

static ScreenId s_pending = SCR_COUNT;

// Runs from lv_timer_handler (not inside an event of the old page), so the old
// page can be deleted *before* the new one is built: only dash + one page in RAM.
static void gotoNow(void*) {
  ScreenId id = s_pending;
  s_pending = SCR_COUNT;
  if (id == s_current || id >= SCR_COUNT) return;
  const ScreenId old = s_current;
  if (old == SCR_SETTINGS) settings_on_leave();

  if (old != SCR_DASH && s_screens[old]) {
    lv_screen_load(s_screens[SCR_DASH]);  // the active screen cannot be deleted
    lv_obj_delete(s_screens[old]);
    s_screens[old] = nullptr;
    s_current = SCR_DASH;
  }
  // Not enough memory for another page (e.g. Bluetooth busy): stay on the dashboard
  // instead of running out of memory halfway through building it.
  if (id != SCR_DASH && !memguard::roomForPage()) {
    LOGF("[ui] page %d refused: free %u KB, largest %u KB\n", (int)id, (unsigned)(memguard::freeHeap() / 1024),
         (unsigned)(memguard::largestBlock() / 1024));
    uiToast("Not enough memory - try again", 0xFFC81E);
    id = SCR_DASH;
  }
  if (id != SCR_DASH) s_screens[id] = createPage(id);

  lv_screen_load(s_screens[id]);
  s_current = id;

  LOGF("[ui] page %d, free heap %u KB (largest block %u KB)\n", (int)id, (unsigned)(memguard::freeHeap() / 1024),
       (unsigned)(memguard::largestBlock() / 1024));

  const uint32_t now = millis();
  switch (id) {
    case SCR_DASH:     dash_update(now); break;
    case SCR_STATS:    stats_update(now); break;
    case SCR_LIVE:     live_update(now); break;
    case SCR_CHART:    chart_on_show(); break;
    case SCR_SETTINGS: settings_update(now); break;
    default: break;
  }
}

void ui_goto(ScreenId id) {
  if (id >= SCR_COUNT) return;
  if (s_pending == SCR_COUNT) lv_async_call(gotoNow, nullptr);
  s_pending = id;
}

// -----------------------------------------------------------------------------
//  Dialogs, toast, pairing PIN
// -----------------------------------------------------------------------------
typedef void (*ConfirmFn)();

// The action travels with its own OK button, so two open dialogs never mix up their actions.
static void confirmOkCb(lv_event_t* e) {
  lv_obj_t* btn = lv_event_get_target_obj(e);
  lv_obj_t* mbox = lv_obj_get_parent(lv_obj_get_parent(btn));  // button -> footer -> msgbox
  ConfirmFn fn = (ConfirmFn)lv_event_get_user_data(e);
  lv_msgbox_close_async(mbox);
  if (fn) fn();
}

static void msgboxCloseCb(lv_event_t* e) {
  lv_obj_t* btn = lv_event_get_target_obj(e);
  lv_msgbox_close_async(lv_obj_get_parent(lv_obj_get_parent(btn)));
}

static lv_obj_t* makeMsgbox(const char* title, const char* text) {
  lv_obj_t* m = lv_msgbox_create(nullptr);
  lv_obj_set_style_bg_color(m, C_PANEL, 0);
  lv_obj_set_style_border_color(m, C_LINE, 0);
  lv_obj_set_style_border_width(m, 2, 0);
  lv_obj_set_style_text_font(m, F_SMALL, 0);
  lv_obj_set_width(m, SCREEN_W * 4 / 5);
  lv_obj_t* t = lv_msgbox_add_title(m, title);
  lv_obj_set_style_text_font(t, F_BODY, 0);
  lv_msgbox_add_text(m, text);
  return m;
}

static lv_obj_t* footerButton(lv_obj_t* mbox, const char* text, lv_color_t color, lv_event_cb_t cb,
                              void* user = nullptr) {
  lv_obj_t* b = lv_msgbox_add_footer_button(mbox, text);
  lv_obj_set_style_bg_color(b, color, 0);
  lv_obj_set_height(b, BTN_H);
  lv_obj_set_style_text_font(b, F_SMALL, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  return b;
}

void ui_confirm(const char* title, const char* text, const char* okText, void (*onOk)()) {
  lv_obj_t* m = makeMsgbox(title, text);
  footerButton(m, "Cancel", C_PANEL_HI, msgboxCloseCb);
  footerButton(m, okText, C_RED, confirmOkCb, (void*)onOk);
}

bool ui_dialog_allowed() {
  if (memguard::roomForDialog()) return true;
  uiToast("Not enough memory - try again", 0xFFC81E);
  return false;
}

void ui_info(const char* title, const char* text) {
  lv_obj_t* m = makeMsgbox(title, text);
  footerButton(m, "OK", C_ACCENT, msgboxCloseCb);
}

static void toastHide(lv_timer_t* t) {
  lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  lv_timer_pause(t);
}

void uiToast(const char* text, uint32_t rgb, uint32_t ms) {
  if (!s_toast) return;
  lv_label_set_text(s_toast, text);
  lv_obj_set_style_text_color(s_toast, lv_color_hex(rgb), 0);
  lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_toast);
  lv_timer_set_period(s_toastTimer, ms);
  lv_timer_reset(s_toastTimer);
  lv_timer_resume(s_toastTimer);
}

static void buildOverlays() {
  s_toast = ui_label(lv_layer_top(), F_BODY, C_TEXT, "");
  lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x202833), 0);
  lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, 0);
  lv_obj_set_style_border_color(s_toast, C_ACCENT, 0);
  lv_obj_set_style_border_width(s_toast, 1, 0);
  lv_obj_set_style_radius(s_toast, 8, 0);
  lv_obj_set_style_pad_hor(s_toast, PAD * 3, 0);
  lv_obj_set_style_pad_ver(s_toast, PAD * 2, 0);
  lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, HDR_H + PAD);
  lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  s_toastTimer = lv_timer_create(toastHide, 2500, nullptr);
  lv_timer_pause(s_toastTimer);

  s_pinBox = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_pinBox, SCREEN_W * 4 / 5, LV_SIZE_CONTENT);
  lv_obj_center(s_pinBox);
  lv_obj_set_style_bg_color(s_pinBox, C_PANEL, 0);
  lv_obj_set_style_border_color(s_pinBox, C_ACCENT, 0);
  lv_obj_set_style_border_width(s_pinBox, 2, 0);
  lv_obj_set_flex_flow(s_pinBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_pinBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  ui_label(s_pinBox, F_BODY, C_ACCENT, LV_SYMBOL_BLUETOOTH " BLUETOOTH PAIRING");
  s_pinLabel = ui_label(s_pinBox, F_LARGE, C_TEXT, "000 000");
  ui_label(s_pinBox, F_SMALL, C_DIM, "Enter this PIN on your phone");
  lv_obj_add_flag(s_pinBox, LV_OBJ_FLAG_HIDDEN);
}

static void servicePinBox() {
  const bool show = bleBridge.pinRequested();
  const bool shown = !lv_obj_has_flag(s_pinBox, LV_OBJ_FLAG_HIDDEN);
  if (show && !shown) {
    lv_label_set_text_fmt(s_pinLabel, "%03lu %03lu", (unsigned long)(cfg.blePasskey / 1000),
                          (unsigned long)(cfg.blePasskey % 1000));
    lv_obj_remove_flag(s_pinBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_pinBox);
  } else if (!show && shown) {
    lv_obj_add_flag(s_pinBox, LV_OBJ_FLAG_HIDDEN);
  }
}

// -----------------------------------------------------------------------------
//  Theme: the accent colour follows the ride mode
// -----------------------------------------------------------------------------
static uint32_t s_accent = 0x00B4FF;

lv_color_t ui_accent() { return lv_color_hex(s_accent); }
lv_color_t ui_line_color() { return lv_color_mix(lv_color_hex(s_accent), lv_color_hex(0x323A46), 70); }
lv_color_t ui_road_color() { return lv_color_mix(lv_color_hex(s_accent), lv_color_hex(0x2A3038), 50); }

static uint32_t wantedAccent() {
  return (cfg.rideModes && cfg.modeTheme) ? rideModeColor(cfg.rideMode) : 0x00B4FF;
}

static void initTheme() {
  lv_display_t* d = lv_display_get_default();
  lv_display_set_theme(d, lv_theme_default_init(d, C_ACCENT, C_GREEN, true, F_SMALL));
  if (s_toast) lv_obj_set_style_border_color(s_toast, C_ACCENT, 0);
}

// Rebuilds the dashboard with the new colours (called from uiUpdate, outside any event).
static void applyThemeIfChanged() {
  const uint32_t want = wantedAccent();
  if (want == s_accent) return;
  s_accent = want;
  initTheme();
  // Delete the old dashboard *before* building the new one: two of them at the same
  // time (plus Bluetooth) could exhaust the heap. Nothing is drawn in between.
  lv_obj_t* blank = nullptr;
  if (s_current == SCR_DASH) {  // the active screen cannot be deleted
    blank = lv_obj_create(nullptr);
    lv_screen_load(blank);
  }
  lv_obj_delete(s_screens[SCR_DASH]);
  s_screens[SCR_DASH] = dash_create();
  if (s_current == SCR_DASH) lv_screen_load(s_screens[SCR_DASH]);
  if (blank) lv_obj_delete(blank);
  dash_update(millis());
}

// Toasts for ride mode results
static void serviceModeState() {
  static ModeState last = ModeState::OFF;
  static uint8_t lastMode = 0xFF;
  const ModeState st = rideModeState();
  if (st == last && (st != ModeState::ACTIVE || lastMode == cfg.rideMode)) return;
  char b[48];
  if (st == ModeState::ACTIVE && (last != ModeState::ACTIVE || lastMode != cfg.rideMode)) {
    snprintf(b, sizeof(b), "%s mode active", rideModeName(cfg.rideMode));
    uiToast(b, rideModeColor(cfg.rideMode));
  } else if (st == ModeState::FAILED && last != ModeState::FAILED) {
    uiToast("VESC did not accept the ride mode", 0xFF3B30);
  }
  last = st;
  lastMode = cfg.rideMode;
}

void uiBegin() {
  s_accent = wantedAccent();
  initTheme();

  s_screens[SCR_DASH] = dash_create();
  buildOverlays();

  s_current = SCR_DASH;
  lv_screen_load(s_screens[SCR_DASH]);
  dash_update(millis());
  if (cfg.startPage > 0 && cfg.startPage < SCR_SETTINGS) ui_goto((ScreenId)cfg.startPage);
}

void uiUpdate(uint32_t now) {
  static uint32_t lastFast = 0, lastSlow = 0;

  chart_sample(now);

  // The heap ran full and the memory reserve was used: free memory by closing the page.
  if (memguard::takeLowMemoryEvent()) {
    dash_close_dialogs();
    settings_close_dialogs();
    if (s_current != SCR_DASH) ui_goto(SCR_DASH);
    uiToast("Low memory - page closed", 0xFFC81E, 4000);
  }

  if (now - lastFast >= 66) {  // ~15 fps for the dashboard
    lastFast = now;
    if (s_current == SCR_DASH) dash_update(now);
  }

  if (now - lastSlow >= 250) {
    lastSlow = now;
    switch (s_current) {
      case SCR_STATS:    stats_update(now); break;
      case SCR_LIVE:     live_update(now); break;
      case SCR_CHART:    chart_update(now); break;
      case SCR_SETTINGS: settings_update(now); break;
      default: break;
    }
    servicePinBox();
    serviceModeState();
    applyThemeIfChanged();

    // Dim the backlight while standing still and nobody touches the screen.
    const bool idle = cfg.dimMinutes && ride.data().speedKmh < 1.0f &&
                      lv_display_get_inactive_time(nullptr) > cfg.dimMinutes * 60000UL;
    display::setDimmed(idle);

    // Riding again? Bring the dashboard back.
    if (cfg.autoReturn && s_current != SCR_DASH && ride.data().speedKmh > 5.0f &&
        lv_display_get_inactive_time(nullptr) > AUTO_RETURN_MS) {
      ui_goto(SCR_DASH);
    }
  }
}
