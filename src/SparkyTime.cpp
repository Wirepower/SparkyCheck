#include "SparkyTime.h"
#include "AppState.h"
#include "SparkyRtc.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

static long clockDisplayOffsetSeconds(void) {
  long s = (long)AppState_getClockTzOffsetMinutes() * 60L;
  if (AppState_getClockDstExtraHour()) s += 3600L;
  return s;
}

time_t SparkyTime_utcToWallTime(time_t utc) {
  return (time_t)((int64_t)utc + (int64_t)clockDisplayOffsetSeconds());
}

/* Treat struct tm as UTC calendar components → epoch (newlib may omit timegm). */
static time_t sparky_timegm(struct tm* t) {
  struct tm c = *t;
  c.tm_isdst = 0;
  char prev[48];
  const char* old = getenv("TZ");
  if (old) {
    strncpy(prev, old, sizeof(prev) - 1);
    prev[sizeof(prev) - 1] = '\0';
  }
  setenv("TZ", "UTC0", 1);
  tzset();
  time_t r = mktime(&c);
  if (old)
    setenv("TZ", prev, 1);
  else
    setenv("TZ", "", 1);
  tzset();
  return r;
}

static void formatTmPreferred(const struct tm* lt, char* buf, size_t cap) {
  if (!lt || !buf || cap == 0) return;
  if (AppState_getClock12Hour()) {
    int h = lt->tm_hour;
    const char* ap = (h >= 12) ? "PM" : "AM";
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, cap, "%02d/%02d/%04d %02d:%02d:%02d %s", lt->tm_mday, lt->tm_mon + 1, lt->tm_year + 1900, h12,
             lt->tm_min, lt->tm_sec, ap);
  } else {
    snprintf(buf, cap, "%02d/%02d/%04d %02d:%02d:%02d", lt->tm_mday, lt->tm_mon + 1, lt->tm_year + 1900, lt->tm_hour,
             lt->tm_min, lt->tm_sec);
  }
  buf[cap - 1] = '\0';
}

void SparkyTime_formatPreferred(char* buf, size_t cap) {
  if (!buf || cap == 0) return;
  buf[0] = '\0';
  time_t t = time(nullptr);
  if (t < (time_t)100000) {
    snprintf(buf, cap, "(clock not set)");
    return;
  }
  time_t w = SparkyTime_utcToWallTime(t);
  struct tm lt;
  if (!gmtime_r(&w, &lt)) {
    snprintf(buf, cap, "(clock not set)");
    return;
  }
  formatTmPreferred(&lt, buf, cap);
}

void SparkyTime_formatAt(time_t t, char* buf, size_t cap) {
  if (!buf || cap == 0) return;
  buf[0] = '\0';
  if (t < (time_t)100000) {
    snprintf(buf, cap, "(unknown)");
    return;
  }
  time_t w = SparkyTime_utcToWallTime(t);
  struct tm lt;
  if (!gmtime_r(&w, &lt)) {
    snprintf(buf, cap, "(unknown)");
    return;
  }
  formatTmPreferred(&lt, buf, cap);
}

void SparkyTime_addSeconds(long delta_sec) {
  struct timeval tv;
  if (gettimeofday(&tv, nullptr) != 0) return;
  tv.tv_sec += delta_sec;
  if (settimeofday(&tv, nullptr) != 0) return;
  if (SparkyRtc_isPresent()) SparkyRtc_writeFromSystemClock();
}

void SparkyTime_formatStatusBar(char* buf, size_t cap) {
  if (!buf || cap < 8) return;
  buf[0] = '\0';
  time_t t = time(nullptr);
  if (t < (time_t)100000) {
    snprintf(buf, cap, "--:--");
    return;
  }
  time_t w = SparkyTime_utcToWallTime(t);
  struct tm lt;
  if (!gmtime_r(&w, &lt)) {
    snprintf(buf, cap, "--:--");
    return;
  }
  if (AppState_getClock12Hour()) {
    int h = lt.tm_hour;
    bool pm = h >= 12;
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, cap, "%d:%02d%s", h12, lt.tm_min, pm ? "pm" : "am");
  } else {
    snprintf(buf, cap, "%02d:%02d", lt.tm_hour, lt.tm_min);
  }
  buf[cap - 1] = '\0';
}

bool SparkyTime_setSystemUtcFromWallFields(int day, int month, int year, int hour, int min, int sec, bool writeRtcIfPresent) {
  if (day < 1 || day > 31 || month < 1 || month > 12 || year < 2000 || year > 2099) return false;
  if (hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) return false;
  struct tm tt;
  memset(&tt, 0, sizeof(tt));
  tt.tm_mday = day;
  tt.tm_mon = month - 1;
  tt.tm_year = year - 1900;
  tt.tm_hour = hour;
  tt.tm_min = min;
  tt.tm_sec = sec;
  time_t wall = sparky_timegm(&tt);
  if (wall == (time_t)-1) return false;
  int64_t utc64 = (int64_t)wall - (int64_t)clockDisplayOffsetSeconds();
  if (utc64 < 0 || utc64 > (int64_t)4102444800LL) return false;
  struct timeval tv;
  tv.tv_sec = (time_t)utc64;
  tv.tv_usec = 0;
  if (settimeofday(&tv, nullptr) != 0) return false;
  AppState_saveWallClockUtc(tv.tv_sec);
  if (writeRtcIfPresent && SparkyRtc_isPresent()) SparkyRtc_writeFromSystemClock();
  return true;
}

bool SparkyTime_setLocalDateTime(int day, int month, int year, int hour, int min, int sec, bool writeRtcIfPresent) {
  return SparkyTime_setSystemUtcFromWallFields(day, month, year, hour, min, sec, writeRtcIfPresent);
}
