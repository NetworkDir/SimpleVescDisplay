// Raw VESC data (both controllers in dual setups) and link / system diagnostics.
#include "ui_internal.h"
#include <string.h>
#include "../core/ride.h"
#include "../core/settings.h"
#include "../core/sdlog.h"
#include "../vesc/telemetry.h"
#include "../vesc/vesc_protocol.h"
#include "../ble/ble_bridge.h"
#include "../ble/ble_link.h"
#include "../hal/display.h"
#include "../hal/memguard.h"

enum Row {
  R_HDR_VESC, R_VOLT, R_IBATT, R_IMOT, R_PWR, R_DUTY, R_ERPM, R_TFET, R_TMOT, R_AH, R_AHC, R_WH, R_WHC,
  R_TACHO, R_FAULT, R_ID, R_SRC, R_AGE,
  R_HDR_SYS, R_LINK, R_PKTS, R_TMO, R_ERR, R_LAT, R_FW, R_HW, R_CAN, R_BLE, R_SD, R_HEAP, R_HEAPMIN, R_LOWMEM,
  R_LDR, R_BRIGHT, R_UPTIME, R_RESET, R_FWV,
  R_COUNT
};

static const char* const kRowNames[R_COUNT] = {
  "VESC", "Voltage", "Battery A", "Motor A", "Power", "Duty", "ERPM", "ESC temp", "Motor temp",
  "Ah used", "Ah charged", "Wh used", "Wh charged", "Tacho abs", "Fault", "Controller ID", "Source", "Updated",
  "SYSTEM", "Interface", "Packets", "Timeouts", "Errors", "Latency", "VESC FW", "VESC HW", "CAN", "Bluetooth",
  "SD card", "Free heap", "Min free heap", "Low memory", "Light sensor", "Backlight", "Uptime", "Last restart",
  "Display FW",
};

static lv_obj_t* s_table;

static void cell(uint16_t row, uint16_t col, const char* txt) {
  const char* cur = lv_table_get_cell_value(s_table, row, col);
  if (!cur || strcmp(cur, txt) != 0) lv_table_set_cell_value(s_table, row, col, txt);
}

lv_obj_t* live_create() {
  lv_obj_t* scr = ui_make_screen();
  lv_obj_t* c = ui_make_nav(scr, "LIVE DATA");
  lv_obj_set_style_pad_all(c, 0, 0);

  s_table = lv_table_create(c);
  lv_table_set_column_count(s_table, 3);
  lv_table_set_row_count(s_table, R_COUNT);
  const int w = SCREEN_W - 8;
  lv_table_set_column_width(s_table, 0, w * 40 / 100);
  lv_table_set_column_width(s_table, 1, w * 30 / 100);
  lv_table_set_column_width(s_table, 2, w * 30 / 100);
  lv_obj_set_width(s_table, SCREEN_W);
  lv_obj_set_style_bg_opa(s_table, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_table, 0, 0);
  lv_obj_set_style_pad_all(s_table, 0, 0);
  lv_obj_set_style_text_font(s_table, F_SMALL, LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(s_table, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_border_color(s_table, C_LINE, LV_PART_ITEMS);
  lv_obj_set_style_border_width(s_table, 1, LV_PART_ITEMS);
  lv_obj_set_style_border_side(s_table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
  lv_obj_set_style_pad_ver(s_table, PAD - 1, LV_PART_ITEMS);
  lv_obj_set_style_pad_hor(s_table, PAD + 2, LV_PART_ITEMS);
  lv_obj_remove_flag(s_table, LV_OBJ_FLAG_CLICKABLE);

  for (int r = 0; r < R_COUNT; r++) lv_table_set_cell_value(s_table, r, 0, kRowNames[r]);
  lv_table_set_cell_value(s_table, R_HDR_VESC, 1, "#1");
  lv_table_set_cell_value(s_table, R_HDR_VESC, 2, "#2");
  // system rows: one wide value column
  for (int r = R_HDR_SYS; r < R_COUNT; r++) svd_table_set_cell_ctrl(s_table, r, 1, LV_TABLE_CELL_CTRL_MERGE_RIGHT);
  return scr;
}

static void vescColumn(int col, const VescState& s, bool enabled, uint32_t now) {
  char b[32];
  if (!enabled || !s.valid) {
    for (int r = R_VOLT; r <= R_AGE; r++) cell(r, col, "-");
    return;
  }
  const vesc::Values& v = s.v;
  snprintf(b, sizeof(b), "%.1f V", v.voltage);        cell(R_VOLT, col, b);
  snprintf(b, sizeof(b), "%.2f A", v.currentIn);      cell(R_IBATT, col, b);
  snprintf(b, sizeof(b), "%.2f A", v.currentMotor);   cell(R_IMOT, col, b);
  snprintf(b, sizeof(b), "%.0f W", v.voltage * v.currentIn); cell(R_PWR, col, b);
  snprintf(b, sizeof(b), "%.1f %%", v.duty * 100.0f); cell(R_DUTY, col, b);
  snprintf(b, sizeof(b), "%.0f", v.erpm);             cell(R_ERPM, col, b);
  snprintf(b, sizeof(b), "%.1f %s", ui_temp(v.tempFet), ui_temp_unit());   cell(R_TFET, col, b);
  snprintf(b, sizeof(b), "%.1f %s", ui_temp(v.tempMotor), ui_temp_unit()); cell(R_TMOT, col, b);
  snprintf(b, sizeof(b), "%.3f", v.ah);               cell(R_AH, col, b);
  snprintf(b, sizeof(b), "%.3f", v.ahCharged);        cell(R_AHC, col, b);
  snprintf(b, sizeof(b), "%.2f", v.wh);               cell(R_WH, col, b);
  snprintf(b, sizeof(b), "%.2f", v.whCharged);        cell(R_WHC, col, b);
  snprintf(b, sizeof(b), "%ld", (long)(v.hasTachoAbs ? v.tachoAbs : v.tacho)); cell(R_TACHO, col, b);
  cell(R_FAULT, col, vesc::faultName(v.fault));
  snprintf(b, sizeof(b), "%u", v.controllerId);       cell(R_ID, col, b);
  cell(R_SRC, col, s.source == DataSource::CAN_STATUS ? "CAN status" : "poll");
  snprintf(b, sizeof(b), "%lu ms ago", (unsigned long)(now - s.updatedMs)); cell(R_AGE, col, b);
}

void live_update(uint32_t now) {
  char b[48];
  vescColumn(1, telemetry.primary(), true, now);
  vescColumn(2, telemetry.secondary(), telemetry.dual(), now);

  snprintf(b, sizeof(b), "%s%s", cfg.demoMode ? "DEMO" : telemetry.linkName(),
           telemetry.connected(now) ? " (ok)" : " (no data)");
  cell(R_LINK, 1, b);
  snprintf(b, sizeof(b), "%lu", (unsigned long)telemetry.rxPackets());  cell(R_PKTS, 1, b);
  snprintf(b, sizeof(b), "%lu", (unsigned long)telemetry.timeouts());   cell(R_TMO, 1, b);
  snprintf(b, sizeof(b), "%lu", (unsigned long)telemetry.errors());     cell(R_ERR, 1, b);
  snprintf(b, sizeof(b), "%u ms", telemetry.latencyMs());               cell(R_LAT, 1, b);
  const vesc::FwInfo& fw = telemetry.fw();
  if (fw.valid) snprintf(b, sizeof(b), "%u.%02u", fw.major, fw.minor);
  else strcpy(b, "-");
  cell(R_FW, 1, b);
  cell(R_HW, 1, fw.valid && fw.hw[0] ? fw.hw : "-");

  if (telemetry.isCan()) {
    snprintf(b, sizeof(b), "VESC %d, me %u, %lu k%s", telemetry.canPrimaryId(), cfg.canOwnId,
             (unsigned long)canBitrateKbps(cfg.canBitrate), telemetry.canBusOff() ? " BUS OFF" : "");
  } else {
    strcpy(b, "-");
  }
  cell(R_CAN, 1, b);

  if (cfg.commMode == COMM_BLE) snprintf(b, sizeof(b), "link %s %s", BleLink::stateName(), BleLink::peerAddress());
  else if (bleBridge.active()) snprintf(b, sizeof(b), "bridge %s", bleBridge.connected() ? "connected" : "waiting");
  else strcpy(b, "off");
  cell(R_BLE, 1, b);

  switch (sdlogState()) {
    case SdState::OFF:     strcpy(b, "off"); break;
    case SdState::NO_CARD: strcpy(b, "no card"); break;
    case SdState::READY:   strcpy(b, "ready"); break;
    case SdState::LOGGING: snprintf(b, sizeof(b), "%s (%lu)", sdlogFile() + 5, (unsigned long)sdlogLines()); break;
    default:               strcpy(b, "error"); break;
  }
  cell(R_SD, 1, b);

  snprintf(b, sizeof(b), "%u KB (largest %u KB)", (unsigned)(memguard::freeHeap() / 1024),
           (unsigned)(memguard::largestBlock() / 1024));                      cell(R_HEAP, 1, b);
  snprintf(b, sizeof(b), "%u KB", (unsigned)(memguard::minFreeHeap() / 1024)); cell(R_HEAPMIN, 1, b);
  snprintf(b, sizeof(b), "%lu x", (unsigned long)memguard::lowMemoryCount()); cell(R_LOWMEM, 1, b);
  snprintf(b, sizeof(b), "%u", display::ldrRaw());                            cell(R_LDR, 1, b);
  snprintf(b, sizeof(b), "%u %%", display::brightness() * 100 / 255);         cell(R_BRIGHT, 1, b);
  ui_fmt_duration(b, sizeof(b), now / 1000);                                  cell(R_UPTIME, 1, b);
  cell(R_RESET, 1, memguard::resetReasonText());
  cell(R_FWV, 1, FW_VERSION);
}
