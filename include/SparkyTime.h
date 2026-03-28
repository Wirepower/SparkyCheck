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

/** Status bar clock: 12h with am/pm or 24h from AppState; uses TZ offset + DST extras. */
void SparkyTime_formatStatusBar(char* buf, size_t cap);

/** UTC plus configured display offset (minutes + optional DST hour), for filling date/time forms. */
time_t SparkyTime_utcToWallTime(time_t utc);

/**
 * Set true UTC from wall-clock fields (what the user sees) using TZ offset + DST extras in AppState.
 * Call after updating AppState clock offset/DST when saving from admin.
 */
bool SparkyTime_setSystemUtcFromWallFields(int day, int month, int year, int hour, int min, int sec, bool writeRtcIfPresent);

/** Apply local date/time (same rules as admin clock form); optional mirror to RTC. */
bool SparkyTime_setLocalDateTime(int day, int month, int year, int hour, int min, int sec, bool writeRtcIfPresent);

#ifdef __cplusplus
}
#endif
