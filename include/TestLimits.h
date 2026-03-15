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

/** Check if RCD trip time passes (value in ms). */
bool TestLimits_rcdTripTimePass(float ms);

/** EFLI: max impedance (Ω) to pass – reference from standards. */
float TestLimits_efliMaxOhms(void);

bool TestLimits_efliPass(float ohms);

#ifdef __cplusplus
}
#endif
