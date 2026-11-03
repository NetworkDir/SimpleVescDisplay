#include "display.h"
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <SD.h>
#include <SPI.h>
#include "lgfx_config.h"
#include "../core/settings.h"
#include "buzzer.h"
#include "../config/app_config.h"
#include "../ui/images/splash_png.h"

#if LV_COLOR_DEPTH != 16
#error "lv_conf.h: LV_COLOR_DEPTH must be 16 (use the lv_conf.h shipped with this project)"
#endif

static LGFX_SVD gfx;
static uint8_t  s_brightness = 0;
static float    s_ldrFilt = -1;
static bool     s_lvglReady = false;
static bool     s_dimmed = false;
static bool     s_swallowTouch = false;   // the touch that wakes a dimmed screen is not a click
static bool     s_wasPressed = false;
static void*    s_buf1 = nullptr;           // LVGL draw buffers (DMA capable)
static void*    s_buf2 = nullptr;
static uint32_t s_bufBytes = 0;

// Two partial buffers of 20 (3.5") / 24 (2.8") lines. Allocated right after the
// panel init - before Bluetooth and the UI fragment the heap - so the DMA-capable
// blocks are always there. Bigger buffers would not render noticeably faster.
static void allocDrawBuffers() {
  const uint32_t lines = gfx.height() >= 320 ? 20 : 24;
  s_bufBytes = gfx.width() * lines * 2;
  s_buf1 = heap_caps_malloc(s_bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  s_buf2 = heap_caps_malloc(s_bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
}

// -----------------------------------------------------------------------------
//  Glyph buffer
//  LVGL renders every letter into a temporary A8 buffer that it allocates for each
//  label it draws. For the big speed digits that is ~13.5 KB (3.5") / ~7.5 KB (2.8")
//  of *contiguous* heap, up to 15 times a second - with Bluetooth running and a
//  fragmented heap that allocation eventually failed (= reboot while riding).
//  One fixed buffer, allocated at boot, serves all labels (they are drawn one by one).
// -----------------------------------------------------------------------------
#if LVGL_VERSION_MINOR >= 3 && __has_include(<lvgl_private.h>)
#include <lvgl_private.h>
#define SVD_GLYPH_POOL 1
static uint8_t*                s_glyphBuf = nullptr;
static size_t                  s_glyphSize = 0;
static bool                    s_glyphBusy = false;
static lv_draw_buf_malloc_cb_t s_fontMalloc = nullptr;
static lv_draw_buf_free_cb_t   s_fontFree = nullptr;

static void* glyphMalloc(size_t size, lv_color_format_t cf) {
  if (!s_glyphBusy && s_glyphBuf && size <= s_glyphSize) {
    s_glyphBusy = true;
    return s_glyphBuf;
  }
  return s_fontMalloc(size, cf);  // bigger than expected or nested: normal allocation
}

static void glyphFree(void* p) {
  if (p == s_glyphBuf) {
    s_glyphBusy = false;
    return;
  }
  s_fontFree(p);
}

static void allocGlyphBuffer() {
  s_glyphSize = gfx.height() >= 320 ? 16 * 1024 : 9 * 1024;
  s_glyphBuf = (uint8_t*)heap_caps_malloc(s_glyphSize + LV_DRAW_BUF_ALIGN, MALLOC_CAP_8BIT);
}

static void installGlyphBuffer() {  // after lv_init()
  lv_draw_buf_handlers_t* h = lv_draw_buf_get_font_handlers();
  if (!s_glyphBuf || !h || !h->buf_malloc_cb || !h->buf_free_cb) return;
  s_fontMalloc = h->buf_malloc_cb;
  s_fontFree = h->buf_free_cb;
  h->buf_malloc_cb = glyphMalloc;
  h->buf_free_cb = glyphFree;
}
#else
static void allocGlyphBuffer() {}
static void installGlyphBuffer() {}
#endif

static constexpr uint16_t COLOR_WHITE = 0xFFFF;
static constexpr uint16_t COLOR_BLACK = 0x0000;
static constexpr uint16_t COLOR_GREY = 0x8410;

#if defined(TOUCH_GT911) && SVD_DEBUG
// Diagnostics: does the GT911 answer on I2C? (product id register 0x8140 = "911")
static void probeGT911() {
  static const uint8_t addrs[] = {0x5D, 0x14};
  for (uint8_t addr : addrs) {
    const uint8_t reg[2] = {0x81, 0x40};
    uint8_t id[4] = {0, 0, 0, 0};
    bool ok = lgfx::i2c::transactionWriteRead(1, addr, reg, 2, id, 4, 400000).has_value();
    LOGF("[touch] GT911 @0x%02X: %s id=%c%c%c\n", addr, ok ? "ACK" : "no answer", id[0] ? id[0] : '-',
         id[1] ? id[1] : '-', id[2] ? id[2] : '-');
  }
}
#endif

void display::begin() {
  gfx.setPanelOptions(TFT_INVERT_DEFAULT, cfg.swapRB);
  gfx.init();
#if defined(TOUCH_GT911) && SVD_DEBUG
  probeGT911();
#endif
  applyRotation();
  applyInvert();
  gfx.fillScreen(COLOR_BLACK);
  if (cfg.touchCalValid) gfx.setTouchCalibrate(cfg.touchCal);
  s_brightness = cfg.brightness;
  gfx.setBrightness(s_brightness);
  pinMode(PIN_LDR, INPUT);
  allocDrawBuffers();
  allocGlyphBuffer();
  LOGF("[disp] %s %dx%d, SPI %lu MHz, touch %s\n", BOARD_NAME, gfx.width(), gfx.height(),
       (unsigned long)(TFT_SPI_FREQ / 1000000), gfx.touch() ? "configured" : "none");
}

void display::applyRotation() {
  gfx.setRotation((TFT_ROTATION + (cfg.flip ? 2 : 0)) & 3);
  if (s_lvglReady) lv_obj_invalidate(lv_screen_active());
}

void display::applyInvert() {
  gfx.invertDisplay(cfg.invertColors != 0);
}

static bool drawImage(const uint8_t* data, size_t len, bool jpg) {
  const int w = gfx.width(), h = gfx.height();
  // scale 0 = fit to the screen keeping the aspect ratio, centred
  return jpg ? gfx.drawJpg(data, len, 0, 0, w, h, 0, 0, 0.0f, 0.0f, lgfx::middle_center)
             : gfx.drawPng(data, len, 0, 0, w, h, 0, 0, 0.0f, 0.0f, lgfx::middle_center);
}

// splash.png / splash.jpg in the root of the SD card replaces the built-in logo
static bool splashFromSd() {
  SPIClass spi(VSPI);
  spi.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  bool ok = false;
  if (SD.begin(PIN_SD_CS, spi, 20000000)) {
    static const char* const names[] = {"/splash.png", "/splash.jpg", "/svd/splash.png", "/svd/splash.jpg"};
    for (const char* name : names) {
      File f = SD.open(name);
      if (!f) continue;
      const size_t n = f.size();
      uint8_t* buf = (n > 0 && n <= 100 * 1024) ? (uint8_t*)malloc(n) : nullptr;
      if (buf && f.read(buf, n) == n) {
        gfx.fillScreen(COLOR_BLACK);
        ok = drawImage(buf, n, strstr(name, ".jpg") != nullptr);
        LOGF("[disp] splash %s from SD: %s\n", name, ok ? "ok" : "cannot decode");
      } else {
        LOGF("[disp] splash %s from SD: too big (max 100 KB)\n", name);
      }
      free(buf);
      f.close();
      if (ok) break;
    }
    SD.end();
  }
  spi.end();
  return ok;
}

void display::showSplash(const char* footer) {
  gfx.fillScreen(COLOR_BLACK);
  if (cfg.bootLogo) {
    if (!splashFromSd()) drawImage(splash_png, sizeof(splash_png), false);
  } else {
    gfx.setFont(&fonts::Font4);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setTextColor(COLOR_WHITE);
    gfx.drawString(FW_NAME, gfx.width() / 2, gfx.height() / 2);
  }
  gfx.setFont(&fonts::Font2);
  gfx.setTextDatum(lgfx::top_left);
  gfx.setTextColor(COLOR_GREY);
  gfx.drawString(footer, 4, 2);
}

bool display::touchPressed() {
  int32_t x, y;
  return gfx.getTouch(&x, &y) > 0;
}

bool display::touchIsResistive() {
#if defined(TOUCH_GT911)
  return false;
#else
  return true;
#endif
}

uint16_t display::ldrRaw() { return (uint16_t)analogRead(PIN_LDR); }
void display::setDimmed(bool dim) { s_dimmed = dim; }
bool display::dimmed() { return s_dimmed; }
uint8_t display::brightness() { return s_brightness; }

void display::serviceBacklight(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 100) return;
  last = now;

  int target = cfg.brightness;
  if (cfg.autoBrightness) {
    const int raw = analogRead(PIN_LDR);
    if (s_ldrFilt < 0) s_ldrFilt = raw;
    s_ldrFilt += (raw - s_ldrFilt) * 0.08f;
    float t = (s_ldrFilt - LDR_RAW_BRIGHT) / (float)(cfg.ldrDark - LDR_RAW_BRIGHT);  // 0 bright .. 1 dark
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    const int minLevel = cfg.brightness < cfg.autoMinBright ? cfg.brightness : cfg.autoMinBright;
    target = cfg.brightness - (int)(t * (cfg.brightness - minLevel));
  }
  if (s_dimmed && target > 12) target = 12;
  if (target != s_brightness) {  // smooth ramp
    int d = target - s_brightness;
    int step = abs(d) / 4;
    if (step < 1) step = 1;
    s_brightness += d > 0 ? step : -step;
    gfx.setBrightness(s_brightness);
  }
}

bool display::calibrateTouch() {
  if (!gfx.touch()) return false;
  const int w = gfx.width(), h = gfx.height();
  gfx.fillScreen(COLOR_BLACK);
  gfx.setFont(&fonts::Font2);
  gfx.setTextDatum(lgfx::middle_center);
  gfx.setTextColor(COLOR_WHITE);
  gfx.drawString("TOUCH CALIBRATION", w / 2, h / 2 - 20);
  gfx.drawString("Touch the tip of the arrow", w / 2, h / 2 + 4);
  gfx.drawString("in each corner", w / 2, h / 2 + 22);

  uint16_t cal[8];
  gfx.calibrateTouch(cal, COLOR_WHITE, COLOR_BLACK, (w > h ? w : h) / 16);
  memcpy(cfg.touchCal, cal, sizeof(cal));
  cfg.touchCalValid = 1;
  settingsSave();
  LOGF("[touch] calibration: %u %u %u %u %u %u %u %u\n", cal[0], cal[1], cal[2], cal[3], cal[4], cal[5],
       cal[6], cal[7]);

  gfx.fillScreen(COLOR_BLACK);
  while (touchPressed()) delay(10);  // wait for release
  if (s_lvglReady) {
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
  }
  return true;
}

// -----------------------------------------------------------------------------
//  LVGL glue
// -----------------------------------------------------------------------------

static uint32_t tickCb() { return millis(); }

// RGB565 little endian (LVGL) -> big endian (SPI panels), two pixels at a time
static inline void swapBytes(uint8_t* buf, uint32_t px) {
  uint32_t* p32 = (uint32_t*)buf;
  for (uint32_t i = 0; i < px / 2; i++) {
    uint32_t v = p32[i];
    p32[i] = ((v & 0xFF00FF00u) >> 8) | ((v & 0x00FF00FFu) << 8);
  }
  if (px & 1) {
    uint16_t* last = (uint16_t*)buf + px - 1;
    *last = (uint16_t)((*last >> 8) | (*last << 8));
  }
}

static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  const uint32_t w = lv_area_get_width(area);
  const uint32_t h = lv_area_get_height(area);
  swapBytes(px, w * h);
  if (gfx.getStartCount() == 0) gfx.startWrite();
  // DMA runs in the background while LVGL renders into the second buffer.
  gfx.pushImageDMA(area->x1, area->y1, w, h, (const lgfx::swap565_t*)px);
  if (lv_display_flush_is_last(disp)) gfx.endWrite();  // waits for the DMA to finish
  lv_display_flush_ready(disp);
}

static void touchCb(lv_indev_t*, lv_indev_data_t* data) {
  int32_t x, y;
  if (gfx.getTouch(&x, &y)) {
    if (s_dimmed || s_swallowTouch) {  // wake up only
      s_swallowTouch = true;
      lv_display_trigger_activity(nullptr);
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    }
    if (!s_wasPressed && cfg.touchSound && cfg.buzzer) buzzerPlay(BEEP_CLICK);
    s_wasPressed = true;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= gfx.width()) x = gfx.width() - 1;
    if (y >= gfx.height()) y = gfx.height() - 1;
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    s_swallowTouch = false;
    s_wasPressed = false;
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void display::lvglBegin() {
  lv_init();
  lv_tick_set_cb(tickCb);
  installGlyphBuffer();

  lv_display_t* disp = lv_display_create(gfx.width(), gfx.height());

  if (!s_buf1) allocDrawBuffers();
  if (!s_buf1) {
    LOGF("[lvgl] FATAL: no memory for draw buffer\n");
    return;
  }
  const uint32_t bytes = s_bufBytes;
  lv_display_set_buffers(disp, s_buf1, s_buf2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchCb);
  lv_timer_set_period(lv_indev_get_read_timer(indev), 20);

  s_lvglReady = true;
  LOGF("[lvgl] v%d.%d.%d ready, 2 x %lu byte buffers%s\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
       LVGL_VERSION_PATCH, (unsigned long)bytes, s_buf2 ? "" : " (single buffer!)");
}
