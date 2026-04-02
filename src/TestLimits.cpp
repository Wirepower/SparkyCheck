#include "TestLimits.h"

/**
 * Pass/fail limits aligned with AS/NZS 3000:2018 and AS/NZS 3017:2022 (installation
 * verification). EFLI max is a configurable default — compare measured Zs to
 * installation-specific tables. RCD measured trip is compared to the user-entered
 * required maximum in the UI when present; TestLimits_rcdTripTimeMaxMs is fallback only.
 * UPDATABILITY / OTA: bump Standards_getRulesVersion() when limits change.
 */

float TestLimits_continuityMaxOhms(void) {
  return 0.5f;
}

float TestLimits_insulationMinMOhms(void) {
  return 1.0f;
}

float TestLimits_insulationMinMOhmsSheathedHeating(void) {
  return 0.01f;
}

bool TestLimits_continuityPass(float ohms) {
  return ohms >= 0.0f && ohms <= TestLimits_continuityMaxOhms();
}

bool TestLimits_insulationPass(float mOhms, bool is_sheathed_heating) {
  float minM = is_sheathed_heating
    ? TestLimits_insulationMinMOhmsSheathedHeating()
    : TestLimits_insulationMinMOhms();
  return mOhms >= minM;
}

float TestLimits_rcdTripTimeMaxMs(void) {
  return 30.0f;  /* Fallback when no required max captured; typical Type A general 30 mA example — verify per installation. */
}

bool TestLimits_rcdTripTimePass(float ms) {
  return ms >= 0.0f && ms <= TestLimits_rcdTripTimeMaxMs();
}

float TestLimits_efliMaxOhms(void) {
  return 0.4f;   /* Default placeholder; Zs max is circuit-specific (3000:2018 / design). Override via Admin rules. */
}

bool TestLimits_efliPass(float ohms) {
  return ohms >= 0.0f && ohms <= TestLimits_efliMaxOhms();
}
