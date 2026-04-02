/**
 * SparkyCheck – Pass/fail limits from AS/NZS 3000, 3017 (and 3760 in Field mode).
 * Limits are implemented in TestLimits.cpp; update there or via OTA config when
 * standards are revised – see docs/STANDARDS_UPDATE.md.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Continuity: max resistance (ohms) to pass. */
float TestLimits_continuityMaxOhms(void);

/** Insulation resistance: min (MΩ) to pass (general). */
float TestLimits_insulationMinMOhms(void);

/** Insulation resistance: min (MΩ) for sheathed heating elements. */
float TestLimits_insulationMinMOhmsSheathedHeating(void);

/** Check if continuity reading passes (value in ohms). */
bool TestLimits_continuityPass(float ohms);

/** Check if IR passes (value in MΩ). is_sheathed_heating uses lower limit. */
bool TestLimits_insulationPass(float mOhms, bool is_sheathed_heating);

/** RCD trip time: max (ms) to pass – AS/NZS 3000 / 3017. */
float TestLimits_rcdTripTimeMaxMs(void);

/**
 * RCD operating-time limit (ms) from installation/test scenario (typical 3000:2018 values):
 * 40 ms when a 40 ms rule applies at the test current or when testing at 5 × IΔn;
 * otherwise 300 ms (common 1 × IΔn case for 30 mA additional protection). Verify unusual IΔn.
 */
float TestLimits_rcdComputedMaxMs(bool circuitRequires40msAtTestCurrent, bool testAt5xIdn);

/** Check if RCD trip time passes (value in ms) against an explicit max. */
bool TestLimits_rcdTripTimePassWithMax(float ms, float maxMs);

/** Check if RCD trip time passes (value in ms) using default TestLimits_rcdTripTimeMaxMs(). */
bool TestLimits_rcdTripTimePass(float ms);

/** EFLI: max impedance (Ω) to pass – reference from standards. */
float TestLimits_efliMaxOhms(void);

bool TestLimits_efliPass(float ohms);

#ifdef __cplusplus
}
#endif
