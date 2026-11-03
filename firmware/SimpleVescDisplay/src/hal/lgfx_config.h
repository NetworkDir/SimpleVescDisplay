#pragma once
// =============================================================================
//  LovyanGFX device description for every supported board.
//  Everything is configured in code - no User_Setup.h editing needed.
// =============================================================================
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "../config/board_config.h"

#if defined(TOUCH_XPT2046_SOFTSPI)
// -----------------------------------------------------------------------------
// XPT2046 driven by bit-banged SPI.
// On the ESP32-2432S028R the touch controller has its own pins (25/32/39/33).
// The stock LovyanGFX driver would need a hardware SPI host for it, which then
// blocks the SD card slot. Bit-banging keeps VSPI free for the SD card and
// still plugs into LovyanGFX, so calibration / rotation work unchanged.
// -----------------------------------------------------------------------------
class Touch_XPT2046_SoftSPI : public lgfx::ITouch {
 public:
  Touch_XPT2046_SoftSPI() {
    _cfg.x_min = 300;
    _cfg.x_max = 3900;
    _cfg.y_min = 200;
    _cfg.y_max = 3700;
    _cfg.bus_shared = false;
  }

  bool init(void) override {
    _inited = false;
    if (_cfg.pin_cs < 0 || _cfg.pin_sclk < 0 || _cfg.pin_mosi < 0 || _cfg.pin_miso < 0) return false;
    lgfx::gpio_hi(_cfg.pin_cs);
    lgfx::pinMode(_cfg.pin_cs, lgfx::pin_mode_t::output);
    lgfx::gpio_lo(_cfg.pin_sclk);
    lgfx::pinMode(_cfg.pin_sclk, lgfx::pin_mode_t::output);
    lgfx::gpio_lo(_cfg.pin_mosi);
    lgfx::pinMode(_cfg.pin_mosi, lgfx::pin_mode_t::output);
    lgfx::pinMode(_cfg.pin_miso, lgfx::pin_mode_t::input);
    if (_cfg.pin_int >= 0) lgfx::pinMode(_cfg.pin_int, lgfx::pin_mode_t::input);
    _inited = true;
    return true;
  }

  void wakeup(void) override {}
  void sleep(void) override {}

  uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t count) override {
    if (!_inited || count == 0) return 0;
    tp->size = 0;
    if (_cfg.pin_int >= 0 && lgfx::gpio_in(_cfg.pin_int)) return 0;  // IRQ high = not touched

    // Same measurement sequence as the stock LovyanGFX XPT2046 driver:
    // 7 x (Y, Z1, X, Z2) samples, then power down.
    uint8_t data[57];
    memset(data, 0, 8);
    data[0] = 0x91;
    data[2] = 0xB1;
    data[4] = 0xD1;
    data[6] = 0xC1;
    data[56] = 0x80;
    memcpy(&data[8], data, 8);
    memcpy(&data[16], data, 16);
    memcpy(&data[32], data, 24);

    lgfx::gpio_lo(_cfg.pin_cs);
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = transfer(data[i]);
    lgfx::gpio_hi(_cfg.pin_cs);

    size_t ix = 0, iy = 0, iz = 0;
    uint_fast16_t xt[7], yt[7], zt[7];
    for (size_t j = 0; j < 7; ++j) {
      const uint8_t* d = &data[j * 8];
      int x = (d[5] << 8 | d[6]) >> 3;
      int y = (d[1] << 8 | d[2]) >> 3;
      int z = 0x3200 + y - x + (((d[3] << 8 | d[4]) - (d[7] << 8 | d[8])) >> 1);
      if (x > 128 && x <= 3968) xt[ix++] = x;
      if (y > 128 && y <= 3968) yt[iy++] = y;
      if (z > 0) zt[iz++] = z;
    }
    if (ix < 3 || iy < 3 || iz < 3) return 0;
    std::sort(xt, xt + ix);
    std::sort(yt, yt + iy);
    std::sort(zt, zt + iz);
    tp->x = xt[ix >> 1];
    tp->y = yt[iy >> 1];
    tp->size = zt[iz >> 1] >> 8;
    return tp->size ? 1 : 0;
  }

 private:
  // SPI mode 0, MSB first, ~1 MHz (XPT2046 max is 2.5 MHz)
  uint8_t transfer(uint8_t out) {
    uint8_t in = 0;
    for (int i = 7; i >= 0; --i) {
      if (out & (1 << i)) lgfx::gpio_hi(_cfg.pin_mosi);
      else                lgfx::gpio_lo(_cfg.pin_mosi);
      delayMicroseconds(1);
      lgfx::gpio_hi(_cfg.pin_sclk);
      delayMicroseconds(1);
      in = (in << 1) | (lgfx::gpio_in(_cfg.pin_miso) ? 1 : 0);
      lgfx::gpio_lo(_cfg.pin_sclk);
    }
    return in;
  }
};
#endif  // TOUCH_XPT2046_SOFTSPI

// -----------------------------------------------------------------------------

class LGFX_SVD : public lgfx::LGFX_Device {
#if defined(TFT_PANEL_ILI9341)
  lgfx::Panel_ILI9341 _panel;
#elif defined(TFT_PANEL_ST7789)
  lgfx::Panel_ST7789 _panel;
#elif defined(TFT_PANEL_ST7796)
  lgfx::Panel_ST7796 _panel;
#endif
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
#if defined(TOUCH_XPT2046_SOFTSPI)
  Touch_XPT2046_SoftSPI _touch;
#elif defined(TOUCH_XPT2046_SHARED)
  lgfx::Touch_XPT2046 _touch;
#elif defined(TOUCH_GT911)
  lgfx::Touch_GT911 _touch;
#endif

 public:
  LGFX_SVD() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;  // "HSPI" - pins 12/13/14/15 are its native IO_MUX pins
      cfg.spi_mode = 0;
      cfg.freq_write = TFT_SPI_FREQ;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_TFT_SCLK;
      cfg.pin_mosi = PIN_TFT_MOSI;
      cfg.pin_miso = PIN_TFT_MISO;
      cfg.pin_dc = PIN_TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = PIN_TFT_CS;
      cfg.pin_rst = PIN_TFT_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = TFT_NATIVE_WIDTH;
      cfg.panel_height = TFT_NATIVE_HEIGHT;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = TFT_INVERT_DEFAULT;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
#if defined(TOUCH_XPT2046_SHARED)
      cfg.bus_shared = true;   // touch controller is on the same SPI bus
#else
      cfg.bus_shared = false;
#endif
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = PIN_TFT_BL;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
#if defined(TOUCH_XPT2046_SOFTSPI)
    {
      auto cfg = _touch.config();
      cfg.x_min = TOUCH_RAW_X_MIN;
      cfg.x_max = TOUCH_RAW_X_MAX;
      cfg.y_min = TOUCH_RAW_Y_MIN;
      cfg.y_max = TOUCH_RAW_Y_MAX;
      cfg.pin_int = PIN_TOUCH_IRQ;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.spi_host = -1;
      cfg.pin_sclk = PIN_TOUCH_SCLK;
      cfg.pin_mosi = PIN_TOUCH_MOSI;
      cfg.pin_miso = PIN_TOUCH_MISO;
      cfg.pin_cs = PIN_TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
#elif defined(TOUCH_XPT2046_SHARED)
    {
      auto cfg = _touch.config();
      cfg.x_min = TOUCH_RAW_X_MIN;
      cfg.x_max = TOUCH_RAW_X_MAX;
      cfg.y_min = TOUCH_RAW_Y_MIN;
      cfg.y_max = TOUCH_RAW_Y_MAX;
      cfg.pin_int = PIN_TOUCH_IRQ;
      cfg.bus_shared = true;
      cfg.offset_rotation = 0;
      cfg.spi_host = SPI2_HOST;
      cfg.freq = 1000000;
      cfg.pin_sclk = PIN_TFT_SCLK;
      cfg.pin_mosi = PIN_TFT_MOSI;
      cfg.pin_miso = PIN_TFT_MISO;
      cfg.pin_cs = PIN_TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
#elif defined(TOUCH_GT911)
    {
      auto cfg = _touch.config();
      cfg.x_min = 0;
      cfg.x_max = TFT_NATIVE_WIDTH - 1;
      cfg.y_min = 0;
      cfg.y_max = TFT_NATIVE_HEIGHT - 1;
      // Polled over I2C instead of waiting for INT: the INT line does not signal
      // reliably on every board revision (touch would never be read).
      cfg.pin_int = -1;
      cfg.pin_rst = PIN_TOUCH_RST;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 1;
      cfg.i2c_addr = 0x5D;     // driver also tries 0x14 automatically
      cfg.pin_sda = PIN_TOUCH_SDA;
      cfg.pin_scl = PIN_TOUCH_SCL;
      cfg.freq = 400000;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
#endif
    setPanel(&_panel);
  }

  // Must be called before init(): colour options that live in the panel config.
  void setPanelOptions(bool invert, bool swapRB) {
    auto cfg = _panel.config();
    cfg.invert = invert;
    cfg.rgb_order = swapRB;
    _panel.config(cfg);
  }
};
