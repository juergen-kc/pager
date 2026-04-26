#include "system/clock.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <time.h>

namespace pager::system_clock {

namespace {

// Anchor: when we last synced, the epoch was g_baseEpoch and the local
// millis() reading was g_baseMillis. nowEpoch() extrapolates from there.
// The BM8563 is also set so wall time survives a soft reboot, but we read
// time back from the millis anchor (avoids timegm() portability across
// newlib variants and keeps the format-time path branch-free).
bool     g_hasSync     = false;
uint32_t g_baseEpoch   = 0;
uint32_t g_baseMillis  = 0;
int32_t  g_tzOffset    = 0;

} // namespace

void begin() {
  // M5.begin() initialises the BM8563. Nothing else needed here — we treat
  // the desktop as the time authority once it sends a sync.
}

void applyTime(uint32_t epoch, int32_t tzOffsetSeconds) {
  g_baseEpoch  = epoch;
  g_baseMillis = millis();
  g_tzOffset   = tzOffsetSeconds;
  g_hasSync    = true;

  // Mirror to the BM8563 in UTC. Useful for diagnostics and any future
  // feature that wants wall time after an unexpected reboot before the
  // desktop reconnects.
  time_t t = static_cast<time_t>(epoch);
  struct tm utc;
  gmtime_r(&t, &utc);

  m5::rtc_datetime_t rtc{};
  rtc.date.year    = utc.tm_year + 1900;
  rtc.date.month   = utc.tm_mon + 1;
  rtc.date.date    = utc.tm_mday;
  rtc.date.weekDay = utc.tm_wday;
  rtc.time.hours   = utc.tm_hour;
  rtc.time.minutes = utc.tm_min;
  rtc.time.seconds = utc.tm_sec;
  M5.Rtc.setDateTime(rtc);
}

bool hasSync() { return g_hasSync; }

uint32_t nowEpoch() {
  if (!g_hasSync) return millis() / 1000;
  return g_baseEpoch + (millis() - g_baseMillis) / 1000;
}

std::string formatHHMM(uint32_t epoch) {
  if (!g_hasSync) return "--:--";
  time_t t = static_cast<time_t>(epoch + g_tzOffset);
  struct tm tm;
  gmtime_r(&t, &tm);
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
  return std::string(buf);
}

std::string nowHHMM() { return formatHHMM(nowEpoch()); }

std::string nowDate() {
  if (!g_hasSync) return "--";
  time_t t = static_cast<time_t>(nowEpoch() + g_tzOffset);
  struct tm tm;
  gmtime_r(&t, &tm);
  char buf[16];
  strftime(buf, sizeof(buf), "%a %d %b", &tm);
  return std::string(buf);
}

} // namespace pager::system_clock
