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
  "RCD operation",
  "SWP D/R (motor)",
  "SWP D/R (appliance)",
  "SWP D/R (heater/sheathed)"
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
  { STEP_INFO, "Installation context", "Determine installation-specific RCD requirements before testing. Check AS/NZS 3000 Clause 2.6 and local/state regulations (special locations such as medical areas can require different settings, e.g. 10 mA).", "AS/NZS 3000 Clause 2.6; AS/NZS 3017", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Requirements confirmed", "Have you confirmed the required RCD rating and required maximum trip/disconnection time for this installation?", "AS/NZS 3000 Clause 2.6; AS/NZS 3017", RESULT_NONE, NULL, NULL },
  { STEP_RESULT_ENTRY, "Required maximum trip time", "Enter the REQUIRED maximum trip/disconnection time for this installation from the applicable regulation.", "AS/NZS 3000 Clause 2.6; AS/NZS 3017", RESULT_RCD_REQUIRED_MAX_MS, "RCD required maximum", "ms" },
  { STEP_RESULT_ENTRY, "RCD trip time", "Perform RCD operation test and enter measured trip time.", "AS/NZS 3000 Clause 8.3.10; 3017", RESULT_RCD_MS, "RCD trip time", "ms" },
};

static const VerifyStep s_swp_motor[] = {
  { STEP_SAFETY, "Start safe", "Wear PPE, control hazards, and notify nearby people before starting motor disconnect/reconnect work.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Prepare tools", "Confirm approved voltage tester, continuity tester, IR tester, insulated tools, lock, and DANGER tags are ready.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Tester battery/check", "Check tester body/leads/fuse for damage and confirm battery/self-test is OK before use.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "1) Positive ID", "Identify the correct motor, all isolation points, and all energy sources (mains, generators, batteries, capacitors, other feeds).", "AS/NZS 4836 Clause 3.1.2", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "2) Isolate", "Open isolator/circuit breaker to physically disconnect supply. If multiple feeds exist, isolate every feed.", "AS/NZS 4836 Clause 3.2.1", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "3) Lock + tag", "Apply personal lock and DANGER tag (name/date/reason). Keep key with you to prevent re-energisation.", "AS/NZS 4836 Clause 3.2.8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "4) Prove tester", "Test voltage detector on a known live source before dead-testing. Use approved detector only.", "AS/NZS 4836 Clause 3.2.6", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "5) Test for dead", "At motor terminals test all relevant combinations: phase-phase, phase-neutral, phase-earth, neutral-earth.", "AS/NZS 4836 Clause 3.2.3", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "6) Prove tester again", "Re-test voltage detector on known live source after dead-testing to confirm tester still works.", "AS/NZS 4836 Clause 3.2.6", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Disconnect and label", "Disconnect motor conductors and label each wire/terminal (photo or tags) to prevent reconnection errors.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Reconnect wiring", "Reconnect wires to correct terminals from manufacturer wiring diagram. Tighten all terminals securely.", "AS/NZS 3000 Section 8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Zero continuity leads", "Zero/compensate continuity leads before measuring. Confirm lead-zero value is stable.", "AS/NZS 3000 Clause 8.3.5; AS/NZS 3017", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Earth continuity check", "Measure continuity from known earth to motor frame. State reading and units clearly.", "AS/NZS 3000 Clause 8.3.5; AS/NZS 3017", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Insulation resistance check", "If required, perform IR test and record reading with units and pass criterion.", "AS/NZS 3000 Clause 8.3.6; AS/NZS 3017", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Common mistakes check", "Do not skip prove-live/dead/prove-live. Do not use neon screwdrivers/basic meters. Do not remove tags early.", "AS/NZS 4836 Clauses 3.2.3, 3.2.6, 3.2.8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Reassemble safely", "Refit covers/guards, check no tools left inside, and confirm terminals are protected.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Restore supply control", "Warn personnel, remove lock/tag only when safe and authorised, then restore supply.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Final motor check", "Did motor start and run correctly with no abnormal heat, sound, smell, or vibration?", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
};

static const VerifyStep s_swp_appliance[] = {
  { STEP_SAFETY, "Start safe", "Wear PPE, control hazards, and notify nearby people before starting appliance disconnect/reconnect work.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Prepare tools", "Confirm approved voltage tester, continuity tester, IR tester, insulated tools, lock, and DANGER tags are ready.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Tester battery/check", "Check tester body/leads/fuse for damage and confirm battery/self-test is OK before use.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "1) Positive ID", "Identify the correct appliance, all isolation points, and all energy sources feeding the equipment.", "AS/NZS 4836 Clause 3.1.2", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "2) Isolate", "Open isolator/circuit breaker to physically disconnect supply. Isolate all feeds where multiple supplies exist.", "AS/NZS 4836 Clause 3.2.1", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "3) Lock + tag", "Apply personal lock and DANGER tag (name/date/reason) at isolation point. Keep key in your control.", "AS/NZS 4836 Clause 3.2.8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "4) Prove tester", "Test voltage detector on known live source before testing for dead. Use approved detector only.", "AS/NZS 4836 Clause 3.2.6", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "5) Test for dead", "At appliance supply points test phase-phase, phase-neutral, phase-earth, neutral-earth as applicable.", "AS/NZS 4836 Clause 3.2.3", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "6) Prove tester again", "Re-test voltage detector on known live source after dead-testing to confirm detector health.", "AS/NZS 4836 Clause 3.2.6", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Disconnect and label", "Disconnect appliance wiring and label each conductor/terminal before reconnection.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Reconnect wiring", "Reconnect as per manufacturer diagram and check terminal tightness.", "AS/NZS 3000 Section 8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Zero continuity leads", "Zero/compensate continuity leads before testing earth continuity.", "AS/NZS 3000 Clause 8.3.5; AS/NZS 3017", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Earth continuity check", "Measure continuity from known earth to appliance frame and state reading with units.", "AS/NZS 3000 Clause 8.3.5; AS/NZS 3017", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Insulation resistance check", "Run IR test live-to-earth/frame and record reading, units, and pass criterion.", "AS/NZS 3000 Clause 8.3.6; AS/NZS 3017", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Common mistakes check", "Never trust breaker OFF alone. Never skip prove-live/dead/prove-live. Never remove lock/tag early.", "AS/NZS 4836 Clauses 3.2.1, 3.2.3, 3.2.6, 3.2.8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Reassemble safely", "Secure covers and cable clamps/strain relief. Confirm no exposed live parts.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Restore supply control", "Warn personnel, remove lock/tag only when safe and authorised, then restore supply.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Final appliance check", "Is appliance operating correctly and safely with no abnormal heat, smell, noise, or faults?", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
};

static const VerifyStep s_swp_heater_sheathed[] = {
  { STEP_SAFETY, "Start safe", "Wear PPE, control hazards, and notify people before working on heater/sheathed element circuit.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Prepare tools", "Confirm approved voltage tester, continuity tester, IR tester, insulated tools, lock, and DANGER tags are ready.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Tester battery/check", "Check tester body/leads/fuse for damage and confirm battery/self-test is OK before use.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "1) Positive ID", "Identify correct heater/sheathed element, all isolation points, and all energy sources feeding it.", "AS/NZS 4836 Clause 3.1.2", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "2) Isolate", "Open isolator/circuit breaker to physically disconnect supply. Isolate all feeds if more than one source.", "AS/NZS 4836 Clause 3.2.1", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "3) Lock + tag", "Apply personal lock and DANGER tag (name/date/reason). Keep key with you to prevent re-energisation.", "AS/NZS 4836 Clause 3.2.8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "4) Prove tester", "Test voltage detector on known live source before dead-testing. Use approved detector only.", "AS/NZS 4836 Clause 3.2.6", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "5) Test for dead", "At terminals test phase-phase, phase-neutral, phase-earth, neutral-earth as applicable.", "AS/NZS 4836 Clause 3.2.3", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "6) Prove tester again", "Re-test voltage detector on known live source after dead-testing to verify it still works.", "AS/NZS 4836 Clause 3.2.6", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Disconnect and label", "Disconnect element wiring and label wires/terminals before reconnecting.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Reconnect wiring", "Reconnect heater/sheathed element wiring to correct terminals and tighten securely.", "AS/NZS 3000 Section 8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Zero continuity leads", "Zero/compensate continuity leads before continuity measurement.", "AS/NZS 3000 Clause 8.3.5; AS/NZS 3017", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Earth continuity check", "Measure continuity from known earth to frame/sheath and state reading with units.", "AS/NZS 3000 Clause 8.3.5; AS/NZS 3017", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Insulation resistance check", "Perform IR test and record reading with units. For sheathed heating, apply required minimum criterion for this installation.", "AS/NZS 3000 Clause 8.3.6; AS/NZS 3017", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Common mistakes check", "Do not miss extra energy sources, do not use wrong tester, and do not remove tag before work is complete.", "AS/NZS 4836 Clauses 3.1.2, 3.2.6, 3.2.8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Reassemble safely", "Refit all covers and verify no exposed live parts remain.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Restore supply control", "Warn personnel, remove lock/tag only when safe and authorised, then restore supply.", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Final heater check", "Does heater/sheathed element operate correctly and safely with no abnormal condition?", "AS/NZS 4836", RESULT_NONE, NULL, NULL },
};

static const VerifyStep* s_steps[VERIFY_TEST_COUNT] = {
  s_continuity,
  s_insulation,
  s_polarity,
  s_earth_continuity,
  s_circuit_connections,
  s_efli,
  s_rcd,
  s_swp_motor,
  s_swp_appliance,
  s_swp_heater_sheathed
};

static const int s_stepCounts[VERIFY_TEST_COUNT] = {
  sizeof(s_continuity) / sizeof(s_continuity[0]),
  sizeof(s_insulation) / sizeof(s_insulation[0]),
  sizeof(s_polarity) / sizeof(s_polarity[0]),
  sizeof(s_earth_continuity) / sizeof(s_earth_continuity[0]),
  sizeof(s_circuit_connections) / sizeof(s_circuit_connections[0]),
  sizeof(s_efli) / sizeof(s_efli[0]),
  sizeof(s_rcd) / sizeof(s_rcd[0]),
  sizeof(s_swp_motor) / sizeof(s_swp_motor[0]),
  sizeof(s_swp_appliance) / sizeof(s_swp_appliance[0]),
  sizeof(s_swp_heater_sheathed) / sizeof(s_swp_heater_sheathed[0]),
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
    case RESULT_RCD_REQUIRED_MAX_MS: return value > 0.0f;
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
    case RESULT_RCD_REQUIRED_MAX_MS: return "AS/NZS 3000 Clause 2.6";
    case RESULT_RCD_MS: return "AS/NZS 3000 Clause 8.3.10";
    case RESULT_EFLI_OHM: return "AS/NZS 3000 Clause 8.3.9";
    default: return "";
  }
}
