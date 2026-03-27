#include "BatteryStatus.h"

#include <Arduino.h>

#ifndef BATTERY_ADC_PIN
#define BATTERY_ADC_PIN -1
#endif

#ifndef BATTERY_EMPTY_MV
#define BATTERY_EMPTY_MV 3300
#endif

#ifndef BATTERY_FULL_MV
#define BATTERY_FULL_MV 4200
#endif

#ifndef BATTERY_SCALE_NUM
#define BATTERY_SCALE_NUM 1
#endif

#ifndef BATTERY_SCALE_DEN
#define BATTERY_SCALE_DEN 1
#endif

static int s_cachedPct = 0;
static unsigned long s_nextSampleMs = 0;

static int clampPct(int pct) {
  if (pct < 0) return 0;
  if (pct > 100) return 100;
  return pct;
}

bool BatteryStatus_getPercent(int* outPercent) {
  if (BATTERY_ADC_PIN < 0) return false;
  unsigned long now = millis();
  if ((long)(now - s_nextSampleMs) >= 0) {
    uint32_t mv = (uint32_t)analogReadMilliVolts(BATTERY_ADC_PIN);
    mv = (mv * (uint32_t)BATTERY_SCALE_NUM) / (uint32_t)BATTERY_SCALE_DEN;
    int pct = 0;
    if (BATTERY_FULL_MV > BATTERY_EMPTY_MV) {
      pct = (int)(((int32_t)mv - (int32_t)BATTERY_EMPTY_MV) * 100 / (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
    }
    s_cachedPct = clampPct(pct);
    s_nextSampleMs = now + 3000UL;
  }
  if (outPercent) *outPercent = s_cachedPct;
  return true;
}

