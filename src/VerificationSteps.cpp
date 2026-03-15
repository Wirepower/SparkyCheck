/**
 * SparkyCheck – Verification steps from AS/NZS 3000 Section 8 & AS/NZS 3017.
 *
 * UPDATABILITY / OTA: When AS/NZS standards are revised, update step text, clause
 * refs, and test names here. For OTA updates, this file (and TestLimits.cpp) can
 * be replaced or overridden by config so new editions ship without full firmware
 * recompile. Bump Standards_getRulesVersion() in StandardsConfig.cpp when you
 * ship updated steps or limits.
 */

#include "VerificationSteps.h"
#include "TestLimits.h"
#include <string.h>

static const char* s_testNames[] = {
  "Earth continuity (conductors)",
  "Insulation resistance",
  "Polarity",
  "Earth continuity (CPC)",
  "Correct circuit connections",
  "Earth fault loop impedance",
  "RCD operation"
};

static const VerifyStep s_continuity[] = {
  { STEP_SAFETY, "Safety", "Zero your test leads before testing so lead resistance is not included in the result. Ensure leads are rated for the voltage.", "AS/NZS 3000 Sec 8; 3017", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Isolation", "Is the circuit isolated from supply before testing?", "AS/NZS 3000 Clause 8.3", RESULT_NONE, NULL, NULL },
  { STEP_RESULT_ENTRY, "Continuity reading", "Measure resistance of protective earthing conductor (after zeroing leads). Enter value (ohm). Max 0.5 ohm.", "AS/NZS 3000 Clause 8.3.5; 3017", RESULT_CONTINUITY_OHM, "Continuity", "ohm" },
};

static const VerifyStep s_insulation[] = {
  { STEP_SAFETY, "Safety", "Do NOT touch terminals during test. Ensure test leads are rated for 500 V DC. Circuit must be isolated.", "AS/NZS 3000 Sec 8; 3017", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Isolation", "Is the circuit isolated and all loads disconnected?", "AS/NZS 3000 Clause 8.3.6", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Sheathed heating", "Is this test for sheathed heating elements? (Min 0.01 MOhm)", "AS/NZS 3000 Clause 8.3.6", RESULT_NONE, NULL, NULL },
  { STEP_RESULT_ENTRY, "Insulation resistance", "Apply 500 V DC (or as per standard). Enter reading (MOhm). Min 1 MOhm (or 0.01 for sheathed).", "AS/NZS 3000 Clause 8.3.6; 3017", RESULT_IR_MOHM, "Insulation resistance", "MOhm" },
};

static const VerifyStep s_polarity[] = {
  { STEP_SAFETY, "Safety", "Verify circuit is isolated before testing. Ensure correct identification of active and neutral.", "AS/NZS 3000 Clause 8.3.7", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Isolation", "Is the circuit isolated from supply?", "AS/NZS 3000 Clause 8.3", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Polarity correct", "Have you verified that polarity is correct (active/neutral not transposed)?", "AS/NZS 3000 Clause 8.3.7; 3017", RESULT_NONE, NULL, NULL },
};

static const VerifyStep s_earth_continuity[] = {
  { STEP_SAFETY, "Safety", "Zero your test leads before testing so lead resistance is not included in the result. Circuit must be isolated. Ensure CPC is connected.", "AS/NZS 3000 Clause 8.3.5", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Isolation", "Is the circuit isolated from supply?", "AS/NZS 3000 Clause 8.3", RESULT_NONE, NULL, NULL },
  { STEP_RESULT_ENTRY, "Earth continuity", "Measure resistance of circuit protective conductor (after zeroing leads). Enter value (ohm). Max 0.5 ohm.", "AS/NZS 3000 Clause 8.3.5; 3017", RESULT_CONTINUITY_OHM, "Earth continuity", "ohm" },
};

static const VerifyStep s_circuit_connections[] = {
  { STEP_SAFETY, "Safety", "Circuit must be isolated. Verify neutral and earth are not transposed.", "AS/NZS 3000 Clause 8.3.8", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Isolation", "Is the circuit isolated from supply?", "AS/NZS 3000 Clause 8.3", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Connections correct", "Have you verified correct circuit connections (neutral/earth not transposed)?", "AS/NZS 3000 Clause 8.3.8; 3017", RESULT_NONE, NULL, NULL },
};

static const VerifyStep s_efli[] = {
  { STEP_SAFETY, "Safety", "Supply may be live. Use approved tester and leads rated for the voltage. Do not bypass safety.", "AS/NZS 3000 Clause 8.3.9; 3017", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Tester ready", "Is the circuit energised and your earth fault loop impedance tester connected and set correctly?", "AS/NZS 3000 Clause 8.3.9", RESULT_NONE, NULL, NULL },
  { STEP_RESULT_ENTRY, "EFLI reading", "Measure earth fault loop impedance. Enter value (ohm). Check against AS/NZS 3000 tables.", "AS/NZS 3000 Clause 8.3.9; 3017", RESULT_EFLI_OHM, "EFLI", "ohm" },
};

static const VerifyStep s_rcd[] = {
  { STEP_SAFETY, "Safety", "RCD test may trip supply. Ensure no critical loads will be affected. Use RCD tester as per manufacturer.", "AS/NZS 3000 Clause 8.3.10; 3017", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Ready to test", "Is the RCD tester connected and circuit energised? Ready to record trip time?", "AS/NZS 3000 Clause 8.3.10", RESULT_NONE, NULL, NULL },
  { STEP_RESULT_ENTRY, "RCD trip time", "Perform RCD operation test. Enter trip time (ms). Max 30 ms typical.", "AS/NZS 3000 Clause 8.3.10; 3017", RESULT_RCD_MS, "RCD trip time", "ms" },
};

static const VerifyStep* s_steps[VERIFY_TEST_COUNT] = {
  s_continuity,
  s_insulation,
  s_polarity,
  s_earth_continuity,
  s_circuit_connections,
  s_efli,
  s_rcd
};

static const int s_stepCounts[VERIFY_TEST_COUNT] = {
  sizeof(s_continuity) / sizeof(s_continuity[0]),
  sizeof(s_insulation) / sizeof(s_insulation[0]),
  sizeof(s_polarity) / sizeof(s_polarity[0]),
  sizeof(s_earth_continuity) / sizeof(s_earth_continuity[0]),
  sizeof(s_circuit_connections) / sizeof(s_circuit_connections[0]),
  sizeof(s_efli) / sizeof(s_efli[0]),
  sizeof(s_rcd) / sizeof(s_rcd[0]),
};

const char* VerificationSteps_getTestName(VerifyTestId id) {
  if (id < 0 || id >= VERIFY_TEST_COUNT) return "";
  return s_testNames[id];
}

int VerificationSteps_getStepCount(VerifyTestId id) {
  if (id < 0 || id >= VERIFY_TEST_COUNT) return 0;
  return s_stepCounts[id];
}

void VerificationSteps_getStep(VerifyTestId id, int stepIndex, VerifyStep* out) {
  if (!out || id < 0 || id >= VERIFY_TEST_COUNT || stepIndex < 0 || stepIndex >= s_stepCounts[id]) return;
  memcpy(out, &s_steps[id][stepIndex], sizeof(VerifyStep));
}

bool VerificationSteps_validateResult(VerifyResultKind kind, float value, bool isSheathedHeating) {
  switch (kind) {
    case RESULT_CONTINUITY_OHM: return TestLimits_continuityPass(value);
    case RESULT_IR_MOHM: return TestLimits_insulationPass(value, false);
    case RESULT_IR_MOHM_SHEATHED: return TestLimits_insulationPass(value, true);
    case RESULT_RCD_MS: return TestLimits_rcdTripTimePass(value);
    case RESULT_EFLI_OHM: return TestLimits_efliPass(value);
    default: return false;
  }
}

const char* VerificationSteps_getClauseForResult(VerifyResultKind kind) {
  switch (kind) {
    case RESULT_CONTINUITY_OHM: return "AS/NZS 3000 Clause 8.3.5";
    case RESULT_IR_MOHM:
    case RESULT_IR_MOHM_SHEATHED: return "AS/NZS 3000 Clause 8.3.6";
    case RESULT_RCD_MS: return "AS/NZS 3000 Clause 8.3.10";
    case RESULT_EFLI_OHM: return "AS/NZS 3000 Clause 8.3.9";
    default: return "";
  }
}
