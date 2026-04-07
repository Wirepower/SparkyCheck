#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Call once from setup() when BATTERY_ADC_PIN is set — sets ADC attenuation for accurate mV reads. */
void BatteryStatus_init(void);

// Returns true when battery sensing is configured and a percent is available.
bool BatteryStatus_getPercent(int* outPercent);

/**
 * After the last successful BatteryStatus_getPercent(), scaled millivolts at the pack (divider + BATTERY_SCALE_*).
 * For tuning EMPTY/FULL thresholds in platformio.ini when the gauge looks wrong.
 */
bool BatteryStatus_getLastScaledMillivolts(uint32_t* outMv);

/** True when the last sample looked invalid (pin stuck near GND — wrong pin, RTC /INT, or hardware). */
bool BatteryStatus_isSenseLikelyFault(void);

#ifdef __cplusplus
}
#endif

