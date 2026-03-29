/**
 * PCF85063A RTC on the Waveshare 4.3B I2C bus (SDA=8, SCL=9), same addressing and
 * year encoding as Waveshare 04_RTC_Test (year register = calendar year − 1970).
 * This is separate from the ESP32’s internal RTC (sleep / low-power); the external
 * chip keeps time across main power loss when a coin cell is fitted.
 * RTC_INT is GPIO6 on the schematic; GPIO6 is also used for battery ADC on this build —
 * use I2C polling only (no interrupt line required for clock read/write).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(SPARKYCHECK_PANEL_43B)
/** Call before tft.init(): starts Wire on GPIO8/9 so ESP_Display_Panel can skip I2C host init (RTC + touch + CH422G). */
void SparkyRtc_earlyInitSharedI2c(void);
#endif

void SparkyRtc_init(void);

/** Re-scan addresses only; does not call Wire.begin() (avoids breaking GT911 touch). */
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
