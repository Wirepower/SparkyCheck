#include "TestLimits.h"

/**
 * Pass/fail limits aligned with AS/NZS 3000:2018 Cl.8.3.5/8.3.6 and AS/NZS 3017:2022
 * Cl.4.4/4.5 (installation verification). EFLI max is a configurable default — compare measured Zs to
 * installation-specific tables. RCD measured trip is compared to a limit computed from
 * scenario answers on device, or to a user-entered max when a custom test still uses that step;
 * TestLimits_rcdTripTimeMaxMs is fallback when no max is set.
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
  return 30.0f;  /* Legacy fallback only; factory RCD flow uses computed 40/300 ms. */
}

float TestLimits_rcdComputedMaxMs(bool circuitRequires40msAtTestCurrent, bool testAt5xIdn) {
  if (circuitRequires40msAtTestCurrent || testAt5xIdn) return 40.0f;
  return 300.0f;
}

bool TestLimits_rcdTripTimePassWithMax(float ms, float maxMs) {
  return ms >= 0.0f && ms <= maxMs;
}

bool TestLimits_rcdTripTimePass(float ms) {
  return TestLimits_rcdTripTimePassWithMax(ms, TestLimits_rcdTripTimeMaxMs());
}

float TestLimits_efliMaxOhms(void) {
  return 0.4f;   /* Default placeholder; Zs max is circuit-specific (3000:2018 / design). Override via Admin rules. */
}

bool TestLimits_efliPass(float ohms) {
  return ohms >= 0.0f && ohms <= TestLimits_efliMaxOhms();
}
