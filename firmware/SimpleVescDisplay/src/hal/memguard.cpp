#include "memguard.h"
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_attr.h>
#include "../config/app_config.h"

#if LV_USE_STDLIB_MALLOC != LV_STDLIB_CUSTOM
#error "lv_conf.h: LV_USE_STDLIB_MALLOC must be LV_STDLIB_CUSTOM (use the lv_conf.h shipped with this project)"
#endif

// The reserve is split in two 8 KB blocks: each one can be released and taken back on
// its own, which still works when the heap is fragmented (no 16 KB hole needed).
static constexpr int      RESERVE_PARTS = 2;
static constexpr size_t   RESERVE_PART = 8 * 1024;
static constexpr uint32_t PAGE_MIN_FREE = 22 * 1024;   // free heap needed to open a page
static constexpr uint32_t PAGE_MIN_BLOCK = 10 * 1024;
static constexpr uint32_t DIALOG_MIN_FREE = 12 * 1024; // keyboard, keypad, choosers
static constexpr uint32_t DIALOG_MIN_BLOCK = 6 * 1024;
static constexpr uint32_t RETAKE_MARGIN = 8 * 1024;    // free heap left after taking a part back

static void*             s_reserve[RESERVE_PARTS] = {nullptr, nullptr};
static volatile bool     s_lowEvent = false;
static uint32_t          s_lowCount = 0;
static esp_reset_reason_t s_resetReason = ESP_RST_UNKNOWN;

// Survives a software reset / crash (not a power cycle): what went wrong last time.
static constexpr uint32_t RTC_MAGIC = 0x53564431;  // "SVD1"
static RTC_NOINIT_ATTR uint32_t s_rtcMagic;
static RTC_NOINIT_ATTR uint32_t s_rtcCause;        // 1 = LVGL assert
static uint32_t s_lastCause = 0;

static bool reserveFull() {
  for (void* p : s_reserve) if (!p) return false;
  return true;
}

static bool releaseReserve() {
  for (void*& p : s_reserve) {
    if (p) {
      free(p);
      p = nullptr;
      s_lowEvent = true;
      s_lowCount++;
      return true;
    }
  }
  return false;
}

void memguard::begin() {
  s_resetReason = esp_reset_reason();
  if (s_rtcMagic == RTC_MAGIC) s_lastCause = s_rtcCause;
  s_rtcMagic = RTC_MAGIC;
  s_rtcCause = 0;
  for (void*& p : s_reserve) if (!p) p = malloc(RESERVE_PART);
  LOGF("[mem] last reset: %s, reserve %s\n", resetReasonText(), reserveFull() ? "ok" : "FAILED");
}

void memguard::service(uint32_t now) {
  static uint32_t last = 0;
  if (reserveFull() || now - last < 2000) return;
  last = now;
  // take the reserve back (one part at a time) once there is memory again
  for (void*& p : s_reserve) {
    if (p) continue;
    if (freeHeap() < RESERVE_PART + RETAKE_MARGIN || largestBlock() < RESERVE_PART + 512) break;
    p = malloc(RESERVE_PART);
    if (p) LOGF("[mem] reserve part restored, free %u KB\n", (unsigned)(freeHeap() / 1024));
    break;
  }
}

bool memguard::takeLowMemoryEvent() {
  if (!s_lowEvent) return false;
  s_lowEvent = false;
  LOGF("[mem] LOW MEMORY: free %u KB, largest %u KB\n", (unsigned)(freeHeap() / 1024),
       (unsigned)(largestBlock() / 1024));
  return true;
}

bool memguard::roomForPage() {
  return s_reserve[0] && freeHeap() >= PAGE_MIN_FREE && largestBlock() >= PAGE_MIN_BLOCK;
}

bool memguard::roomForDialog() {
  return s_reserve[0] && freeHeap() >= DIALOG_MIN_FREE && largestBlock() >= DIALOG_MIN_BLOCK;
}

uint32_t memguard::freeHeap() { return heap_caps_get_free_size(MALLOC_CAP_8BIT); }
uint32_t memguard::largestBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); }
uint32_t memguard::minFreeHeap() { return heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT); }
uint32_t memguard::lowMemoryCount() { return s_lowCount; }

const char* memguard::resetReasonText() {
  if (s_lastCause == 1) return "crash (graphics)";
  switch (s_resetReason) {
    case ESP_RST_POWERON:   return "power on";
    case ESP_RST_EXT:       return "reset pin";
    case ESP_RST_SW:        return "restart";
    case ESP_RST_PANIC:     return "crash";
    case ESP_RST_INT_WDT:   return "watchdog (irq)";
    case ESP_RST_TASK_WDT:  return "watchdog (task)";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT:  return "brownout (power dip)";
    default:                return "unknown";
  }
}

bool memguard::resetWasAbnormal() {
  return s_lastCause != 0 || s_resetReason == ESP_RST_PANIC || s_resetReason == ESP_RST_INT_WDT ||
         s_resetReason == ESP_RST_TASK_WDT || s_resetReason == ESP_RST_WDT || s_resetReason == ESP_RST_BROWNOUT;
}

// -----------------------------------------------------------------------------
//  LVGL memory backend (LV_STDLIB_CUSTOM)
// -----------------------------------------------------------------------------
extern "C" {

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes) {
  LV_UNUSED(mem);
  LV_UNUSED(bytes);
  return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void* lv_malloc_core(size_t size) {
  void* p = malloc(size);
  if (!p && releaseReserve()) p = malloc(size);
  return p;
}

void* lv_realloc_core(void* p, size_t newSize) {
  void* n = realloc(p, newSize);
  if (!n && newSize && releaseReserve()) n = realloc(p, newSize);
  return n;
}

void lv_free_core(void* p) { free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t* mon) {
  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_8BIT);
  mon->total_size = info.total_free_bytes + info.total_allocated_bytes;
  mon->free_size = info.total_free_bytes;
  mon->free_biggest_size = info.largest_free_block;
  mon->free_cnt = info.free_blocks;
  mon->used_cnt = info.allocated_blocks;
  mon->max_used = mon->total_size - info.minimum_free_bytes;
  mon->used_pct = mon->total_size ? (uint8_t)(100 - 100ULL * mon->free_size / mon->total_size) : 0;
  mon->frag_pct = mon->free_size ? (uint8_t)(100 - 100ULL * mon->free_biggest_size / mon->free_size) : 0;
}

lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

// LV_ASSERT_HANDLER (lv_conf.h): remember the cause, log, reboot with a backtrace.
void svd_lv_assert_failed(void) {
  s_rtcMagic = RTC_MAGIC;
  s_rtcCause = 1;
  LOGF("[lvgl] ASSERT failed - free heap %u, largest block %u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  abort();
}

}  // extern "C"
