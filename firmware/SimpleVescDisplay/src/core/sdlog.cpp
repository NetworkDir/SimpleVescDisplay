#include "sdlog.h"
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include "ride.h"
#include "settings.h"
#include "../config/app_config.h"
#include "../config/board_config.h"

static SPIClass  s_spi(VSPI);
static bool      s_mounted = false;
static File      s_file;
static char      s_name[32] = {0};
static char      s_buf[2048];
static size_t    s_len = 0;
static uint32_t  s_lastLine = 0, s_lastFlush = 0, s_lastAttempt = 0, s_lines = 0;
static uint8_t   s_failures = 0;
static SdState   s_state = SdState::OFF;

SdState sdlogState() { return s_state; }
const char* sdlogFile() { return s_name; }
uint32_t sdlogLines() { return s_lines; }

static bool mount() {
  if (s_mounted) return true;
  s_spi.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, s_spi, 20000000)) {
    s_spi.end();
    return false;
  }
  s_mounted = true;
  LOGF("[sd] card mounted, %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));
  return true;
}

static void unmount() {
  if (s_file) s_file.close();
  if (s_mounted) {
    SD.end();
    s_spi.end();
  }
  s_mounted = false;
  s_name[0] = 0;
  s_len = 0;
}

// The next file number is kept in NVS: probing ride_0001 ... ride_0500 one by one on
// a full card blocked the loop for seconds and allocated a file object per probe.
static bool openNewFile() {
  SD.mkdir("/svd");
  Preferences p;
  uint16_t idx = 1;
  if (p.begin("svd", true)) {
    idx = p.getUShort("logidx", 1);
    p.end();
  }
  if (idx < 1 || idx > 9999) idx = 1;
  for (int tries = 0; tries < 20; tries++) {  // a few files may exist from another display
    snprintf(s_name, sizeof(s_name), "/svd/ride_%04u.csv", (unsigned)idx);
    if (!SD.exists(s_name)) break;
    idx = idx >= 9999 ? 1 : idx + 1;
  }
  s_file = SD.open(s_name, FILE_WRITE);
  if (!s_file) {
    s_name[0] = 0;
    return false;
  }
  if (p.begin("svd", false)) {
    p.putUShort("logidx", idx >= 9999 ? 1 : idx + 1);
    p.end();
  }
  s_file.println("time_s,speed_kmh,voltage,batt_pct,current_in,current_motor,power_w,duty_pct,erpm,"
                 "temp_fet,temp_motor,trip_km,odo_km,wh_used,wh_regen,fault");
  s_file.flush();
  s_lines = 0;
  LOGF("[sd] logging to %s\n", s_name);
  return true;
}

static bool flushBuffer() {
  if (!s_len) return true;
  bool ok = s_file && s_file.write((const uint8_t*)s_buf, s_len) == s_len;
  if (ok) s_file.flush();
  s_len = 0;
  return ok;
}

void sdlogUpdate(uint32_t now) {
  if (!cfg.sdLog) {
    if (s_mounted) {
      flushBuffer();
      unmount();
    }
    s_state = SdState::OFF;
    s_failures = 0;
    return;
  }

  const RideData& r = ride.data();

  if (!s_file) {
    // mounting blocks for a moment without a card: retry rarely and give up after 3 tries
    if (s_failures >= 3 || now - s_lastAttempt < 5000) return;
    s_lastAttempt = now;
    if (ESP.getFreeHeap() < 30 * 1024) return;  // mounting + a file need ~10 KB: wait for memory
    if (!mount()) {
      s_failures++;
      s_state = SdState::NO_CARD;
      return;
    }
    if (!openNewFile()) {
      s_failures++;
      s_state = SdState::ERROR;
      unmount();
      return;
    }
    s_failures = 0;
    s_lastFlush = now;
  }

  if (!r.connected) {
    s_state = SdState::READY;
    if (s_len && now - s_lastFlush > 2000) {
      flushBuffer();
      s_lastFlush = now;
    }
    return;
  }
  s_state = SdState::LOGGING;

  if (now - s_lastLine >= 200 && s_len < sizeof(s_buf) - 200) {
    s_lastLine = now;
    const size_t room = sizeof(s_buf) - s_len;
    const int n = snprintf(s_buf + s_len, room,
                           "%.1f,%.1f,%.1f,%.0f,%.1f,%.1f,%.0f,%.1f,%.0f,%.1f,%.1f,%.3f,%.2f,%.2f,%.2f,%u\n",
                           now / 1000.0f, r.speedKmh, r.voltage, r.battPct, r.currentIn, r.currentMotor, r.powerW,
                           r.dutyPct, r.erpm, r.tempFet, r.tempMotor, r.tripKm, r.odoKm, r.whUsed, r.whRegen,
                           (unsigned)r.fault);
    // snprintf returns the length it *wanted*: a line that did not fit (garbage values)
    // must not move s_len past the buffer
    if (n > 0 && (size_t)n < room) {
      s_len += n;
      s_lines++;
    }
  }

  if (s_len > 1500 || (s_len && now - s_lastFlush > 5000)) {
    s_lastFlush = now;
    if (!flushBuffer()) {  // card removed?
      LOGF("[sd] write failed, closing\n");
      unmount();
      s_state = SdState::ERROR;
    }
  }
}
