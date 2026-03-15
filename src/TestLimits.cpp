#include "TestLimits.h"

/**
 * Pass/fail limits from AS/NZS 3000, 3017 (and 3760 in Field mode).
 * UPDATABILITY / OTA: When standards are revised, update limits here. For OTA
 * updates, this file can be replaced or limits loaded from config; bump
 * Standards_getRulesVersion() when you ship new limits.
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
  return 30.0f;  /* AS/NZS 3000 / 3017 typical requirement; update if standard specifies otherwise */
}

bool TestLimits_rcdTripTimePass(float ms) {
  return ms >= 0.0f && ms <= TestLimits_rcdTripTimeMaxMs();
}

float TestLimits_efliMaxOhms(void) {
  return 0.4f;   /* Example; varies by circuit – update from AS/NZS 3000 tables */
}

bool TestLimits_efliPass(float ohms) {
  return ohms >= 0.0f && ohms <= TestLimits_efliMaxOhms();
}
