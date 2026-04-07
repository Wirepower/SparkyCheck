#include "BatteryStatus.h"

#include <Arduino.h>
#include "driver/gpio.h"

#if defined(SPARKYCHECK_PANEL_43B) && BATTERY_ADC_PIN >= 0
#include "SparkyRtc.h"
#endif

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
static uint32_t s_lastScaledMv = 0;
static bool s_senseFault = false;
static unsigned long s_nextSampleMs = 0;

static int clampPct(int pct) {
  if (pct < 0) return 0;
  if (pct > 100) return 100;
  return pct;
}

void BatteryStatus_init(void) {
#if BATTERY_ADC_PIN >= 0
  analogSetAttenuation(ADC_11db);
  analogSetPinAttenuation((uint8_t)BATTERY_ADC_PIN, ADC_11db);
#endif
}

bool BatteryStatus_getLastScaledMillivolts(uint32_t* outMv) {
  if (BATTERY_ADC_PIN < 0 || !outMv) return false;
  *outMv = s_lastScaledMv;
  return true;
}

bool BatteryStatus_isSenseLikelyFault(void) {
#if BATTERY_ADC_PIN < 0
  return false;
#else
  return s_senseFault;
#endif
}

bool BatteryStatus_getPercent(int* outPercent) {
  if (BATTERY_ADC_PIN < 0) return false;
  unsigned long now = millis();
  if ((long)(now - s_nextSampleMs) >= 0) {
#if defined(SPARKYCHECK_PANEL_43B) && BATTERY_ADC_PIN >= 0
    SparkyRtc_releaseBatteryAdcSharedPin();
#endif
    const gpio_num_t g = (gpio_num_t)BATTERY_ADC_PIN;
    gpio_hold_dis(g);
    gpio_reset_pin(g);
    pinMode(BATTERY_ADC_PIN, INPUT);
    analogSetPinAttenuation((uint8_t)BATTERY_ADC_PIN, ADC_11db);
    for (int w = 0; w < 24; w++) {
      (void)analogReadMilliVolts(BATTERY_ADC_PIN);
      delayMicroseconds(300);
    }

    uint32_t sumMv = 0;
    constexpr int kSamples = 12;
    for (int i = 0; i < kSamples; i++) {
      sumMv += (uint32_t)analogReadMilliVolts(BATTERY_ADC_PIN);
      if (i + 1 < kSamples) delayMicroseconds(400);
    }
    uint32_t pinMv = sumMv / (uint32_t)kSamples;
    uint32_t mv = (pinMv * (uint32_t)BATTERY_SCALE_NUM) / (uint32_t)BATTERY_SCALE_DEN;
    s_lastScaledMv = mv;
    /*
     * Below ~450 mV "pack" after scale is not a real 1S LiPo reading (empty is still ~3.3 V pack).
     * Treat as hardware/pin conflict (e.g. /INT stuck low) so UI does not show "dead" battery.
     */
    s_senseFault = (mv < 450u);
    int pct = 0;
    if (s_senseFault) {
      pct = 0;
      static bool s_loggedFault;
      if (!s_loggedFault) {
        s_loggedFault = true;
        Serial.printf("[Battery] sense fault: pin~%u mV scaled~%u mV (GPIO%d). Measure pin vs GND; expect ~1–2.1 V tap.\n",
                      (unsigned)pinMv, (unsigned)mv, BATTERY_ADC_PIN);
      }
    } else if (BATTERY_FULL_MV > BATTERY_EMPTY_MV) {
      pct = (int)(((int32_t)mv - (int32_t)BATTERY_EMPTY_MV) * 100 / (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
    }
    s_cachedPct = clampPct(pct);
    s_nextSampleMs = now + 3000UL;
  }
  if (outPercent) *outPercent = s_cachedPct;
  return true;
}
