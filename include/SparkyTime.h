#pragma once

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** dd/mm/yyyy hh:mm:ss with AM/PM when 12h is on; 24h clock when off. */
void SparkyTime_formatPreferred(char* buf, size_t cap);

/** Same formatting rules for an arbitrary instant (e.g. file mtime). */
void SparkyTime_formatAt(time_t t, char* buf, size_t cap);

/** Shift system clock; updates RTC if present. */
void SparkyTime_addSeconds(long delta_sec);

/** Compact 12h clock for status bar: `3:05pm` style (empty/lowercase am/pm). */
void SparkyTime_formatStatusBar12(char* buf, size_t cap);

/** Apply local date/time (same rules as admin clock form); optional mirror to RTC. */
bool SparkyTime_setLocalDateTime(int day, int month, int year, int hour, int min, int sec, bool writeRtcIfPresent);

#ifdef __cplusplus
}
#endif
