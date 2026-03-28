/**
 * PCF85063A RTC on the Waveshare 4.3B I2C bus (SDA=8, SCL=9). RTC_INT is GPIO6
 * on the schematic; GPIO6 is also used for battery ADC on this build — use I2C
 * polling only (no interrupt line required for clock read/write).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

void SparkyRtc_init(void);

/** Re-scan I2C (e.g. after touch stack re-inits Wire). Updates presence flag. */
void SparkyRtc_refreshPresence(void);

bool SparkyRtc_isPresent(void);

/** Read date/time from the RTC into local broken-down time (same as device clock). */
bool SparkyRtc_readTm(struct tm* out);

/** Write date/time to the RTC (24h mode in hardware). */
bool SparkyRtc_writeTm(const struct tm* t);

/** settimeofday() from RTC; no-op if RTC missing. */
bool SparkyRtc_syncSystemFromRtc(void);

/** Write current system local time to RTC; no-op if RTC missing. */
bool SparkyRtc_writeFromSystemClock(void);

#ifdef __cplusplus
}
#endif
