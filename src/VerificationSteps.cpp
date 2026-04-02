/**
 * SparkyCheck – Verification steps from AS/NZS 3000:2018 Sec 8, AS/NZS 3017:2022,
 * AS/NZS 4836:2023 (SWP), AS/NZS 3760:2022 (in-service checklist).
 *
 * UPDATABILITY / OTA: When AS/NZS standards are revised, update step text, clause
 * refs, and test names here. For OTA updates, this file (and TestLimits.cpp) can
 * be replaced or overridden by config so new editions ship without full firmware
 * recompile. Bump Standards_getRulesVersion() in StandardsConfig.cpp when you
 * ship updated steps or limits.
 */

#include "VerificationSteps.h"
#include "TestLimits.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <memory>

#define VERIFY_MAX_STEPS_PER_TEST 24
#define VERIFY_MAX_NAME_LEN 48
#define VERIFY_MAX_TITLE_LEN 64
#define VERIFY_MAX_INSTR_LEN 256
#define VERIFY_MAX_CLAUSE_LEN 96
#define VERIFY_MAX_LABEL_LEN 40
#define VERIFY_MAX_UNIT_LEN 16

typedef struct {
  VerifyStepType type;
  char title[VERIFY_MAX_TITLE_LEN];
  char instruction[VERIFY_MAX_INSTR_LEN];
  char clause[VERIFY_MAX_CLAUSE_LEN];
  VerifyResultKind resultKind;
  char resultLabel[VERIFY_MAX_LABEL_LEN];
  char unit[VERIFY_MAX_UNIT_LEN];
} CustomVerifyStep;

typedef enum {
  CMP_DEFAULT = 0,
  CMP_LT,
  CMP_GT,
  CMP_LE,
  CMP_GE,
  CMP_EQ,
  CMP_BETWEEN
} CmpOp;

static bool s_customActive = false;
typedef char TestNameBuf[VERIFY_MAX_NAME_LEN];
typedef CustomVerifyStep TestStepsBuf[VERIFY_MAX_STEPS_PER_TEST];
typedef bool TestExpectedBuf[VERIFY_MAX_STEPS_PER_TEST];
static std::unique_ptr<TestNameBuf[]> s_customTestNames;
static std::unique_ptr<TestStepsBuf[]> s_customSteps;
static std::unique_ptr<TestExpectedBuf[]> s_customExpectedYes;
static std::unique_ptr<TestExpectedBuf[]> s_customYesNoBranchOnly;
static std::unique_ptr<int[]> s_customStepCounts;
static int s_customActiveTestCount = 0;
static const int kDefaultTestCount = VERIFY_TEST_COUNT;
static CmpOp s_customOps[RESULT_NONE];
static float s_customVals[RESULT_NONE];
static float s_customValsMax[RESULT_NONE];

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
  "SWP D/R (heater/sheathed)",
  "In-service inspection (3760)"
};

static const VerifyStep s_continuity[] = {
  { STEP_SAFETY, "Safety", "Zero or compensate test leads so lead resistance is excluded. Use leads and instrument suitable for the circuit voltage.", "3000:2018 Sec 8; 3017:2022 Cl.4.4", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Method", "Continuity of protective earthing conductors is verified per 3000:2018 Cl.8.3.5 and tested in 3017:2022 Cl.4.4. Record the value after lead-zero.", "3000:2018 Cl.8.3.5; 3017:2022 Cl.4.4", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Isolation", "Is the circuit isolated from supply before testing?", "3000:2018 Cl.8.3; 3017:2022 Sec 4", RESULT_NONE, NULL, NULL },
  { STEP_RESULT_ENTRY, "Continuity reading", "Enter your reading (ohms). The device shows PASS or FAIL using the rule below (Admin rules or factory default).", "3000:2018 Cl.8.3.5; 3017:2022 Cl.4.4", RESULT_CONTINUITY_OHM, "Continuity", "ohm" },
};

static const VerifyStep s_insulation[] = {
  { STEP_SAFETY, "Safety", "Do not touch live parts. Use 500 V DC rated leads where required. Isolate the circuit and disconnect electronic loads as applicable.", "3000:2018 Sec 8; 3017:2022 Cl.4.5", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "IR test", "Insulation resistance is per 3000:2018 Cl.8.3.6; apply test voltage and stabilise reading per 3017:2022 Cl.4.5 and manufacturer instructions.", "3000:2018 Cl.8.3.6; 3017:2022 Cl.4.5", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Isolation", "Is the circuit isolated and loads disconnected as required for the IR test?", "3000:2018 Cl.8.3.6; 3017:2022 Sec 4", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Sheathed heating", "Is this for sheathed heating elements? (Lower minimum IR applies.) Yes/No only picks the limit — neither answer is a failure.", "3000:2018 Cl.8.3.6; 3017:2022 Cl.4.5", RESULT_NONE, NULL, NULL },
  { STEP_RESULT_ENTRY, "Insulation resistance", "Enter your reading (MOhm). The device shows PASS or FAIL using the rule below (sheathed heating uses the lower minimum).", "3000:2018 Cl.8.3.6; 3017:2022 Cl.4.5", RESULT_IR_MOHM, "Insulation resistance", "MOhm" },
};

static const VerifyStep s_polarity[] = {
  { STEP_SAFETY, "Safety", "Confirm isolation where required. Identify conductors clearly before declaring polarity.", "3000:2018 Cl.8.3.7; 3017:2022 Cl.4.7", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Polarity check", "Verify at every relevant point that active and neutral are not transposed (3000:2018 Cl.8.3.7; 3017:2022 Cl.4.7). Answer the checks honestly—Yes only if correct; No records FAIL.", "3000:2018 Cl.8.3.7; 3017:2022 Cl.4.7", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Isolation", "Is the circuit isolated from supply for your test method?", "3000:2018 Cl.8.3; 3017:2022 Sec 4", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Polarity correct", "Have you verified polarity is correct (active/neutral not transposed) at all required points?", "3000:2018 Cl.8.3.7; 3017:2022 Cl.4.7", RESULT_NONE, NULL, NULL },
};

static const VerifyStep s_earth_continuity[] = {
  { STEP_SAFETY, "Safety", "Zero leads, isolate supply, confirm CPC continuity path is the one under test.", "3000:2018 Cl.8.3.5; 3017:2022 Cl.4.4", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "CPC test", "Measure circuit protective conductor (CPC) resistance per 3000:2018 Cl.8.3.5 after lead compensation.", "3000:2018 Cl.8.3.5; 3017:2022 Cl.4.4", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Isolation", "Is the circuit isolated from supply?", "3000:2018 Cl.8.3; 3017:2022 Sec 4", RESULT_NONE, NULL, NULL },
  { STEP_RESULT_ENTRY, "Earth continuity", "Enter your reading (ohms). The device shows PASS or FAIL using the rule below (Admin rules or factory default).", "3000:2018 Cl.8.3.5; 3017:2022", RESULT_CONTINUITY_OHM, "Earth continuity", "ohm" },
};

static const VerifyStep s_circuit_connections[] = {
  { STEP_SAFETY, "Safety", "Circuit isolated. Check neutral and protective earth are not transposed or combined incorrectly.", "3000:2018 Cl.8.3.8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Connections", "Correct circuit connections are confirmed per 3000:2018 Cl.8.3.8 (neutral/earth identification). Answer honestly—Yes only if correct; No records FAIL.", "3000:2018 Cl.8.3.8", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Isolation", "Is the circuit isolated from supply?", "3000:2018 Cl.8.3; 3017:2022 Sec 4", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Connections correct", "Have you verified correct circuit connections (neutral/earth not transposed)?", "3000:2018 Cl.8.3.8; 3017:2022 Cl.4.7", RESULT_NONE, NULL, NULL },
};

static const VerifyStep s_efli[] = {
  { STEP_SAFETY, "Safety", "Supply may be energised. Use an approved loop tester and leads rated for the installation voltage.", "3000:2018 Cl.8.3.9; 3017:2022 Cl.4.8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Zs criterion", "Confirm your installation’s maximum permitted Zs from AS/NZS 3000:2018 / design tables. The device compares your measured value to the limit below (Admin rules or factory default).", "3000:2018 Cl.8.3.9; 3017:2022 Cl.4.8", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Tester ready", "Is the circuit energised and the loop tester connected and set correctly?", "3000:2018 Cl.8.3.9", RESULT_NONE, NULL, NULL },
  { STEP_RESULT_ENTRY, "EFLI reading", "Enter your measured Zs (ohms). The device shows PASS or FAIL using the rule below (set Admin rules to match your tabulated limit).", "3000:2018 Cl.8.3.9; 3017:2022 Cl.4.8", RESULT_EFLI_OHM, "EFLI", "ohm" },
};

static const VerifyStep s_rcd[] = {
  { STEP_SAFETY, "Safety", "RCD test may de-energise circuits. Warn occupants; avoid critical loads. Use RCD tester per manufacturer.", "3000:2018 Cl.8.3.10; 3017:2022 Cl.4.9", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Before you test", "Use your RCD tester as appropriate for this installation. The next screens ask where the RCD is used and how you are testing—the coach then applies the matching pass rule.", "3017:2022 Cl.4.9; 3000:2018 Cl.8.3.10", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Ready to test", "Is the RCD tester connected correctly and the circuit energised where required?", "3000:2018 Cl.8.3.10; 3017:2022 Cl.4.9", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Your scenario", "Answer honestly. Yes and No only pick which pass rule applies—you are not being graded on these questions.", "3000:2018 Cl.2.6; 3017:2022 Cl.4.9.5", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Scenario: healthcare", "Is this RCD in a hospital, clinic, dental surgery, or other patient / body-protected electrical area (AS/NZS 3003 type locations)?", "3003:2018; 3000:2018 Cl.2.6", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Scenario: tester strength", "On your RCD tester, are you using the stronger trip test (higher test current), not the gentler routine trip test?", "3017:2022 Cl.4.9.5.3", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Scenario: circuit", "Is this only a standard final circuit in a normal house or office (not construction supply, caravan inlet, pool zone, or other special Wiring Rules location)?", "3000:2018 Cl.2.6", RESULT_NONE, NULL, NULL },
  { STEP_RESULT_ENTRY, "RCD trip time", "Run the RCD test; enter the time (ms) your tester shows. PASS/FAIL uses the rule line above from your scenarios.", "3000:2018 Cl.8.3.10; 3017:2022 Cl.4.9.5.3", RESULT_RCD_MS, "RCD trip time", "ms" },
};

static const VerifyStep s_swp_motor[] = {
  { STEP_SAFETY, "Start safe", "Wear PPE, control hazards, and notify nearby people before starting motor disconnect/reconnect work.", "4836:2023 Sec 2; Sec 11", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Prepare tools", "Confirm approved voltage tester, continuity tester, IR tester, insulated tools, lock, and DANGER tags are ready.", "4836:2023 Sec 8; Sec 9", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Tester battery/check", "Check tester body/leads/fuse for damage and confirm battery/self-test is OK before use.", "4836:2023 Sec 9", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "1) Positive ID", "Identify the correct motor, all isolation points, and all energy sources (mains, generators, batteries, capacitors, other feeds).", "4836:2023 Cl.3.1.2", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "2) Isolate", "Open isolator/circuit breaker to physically disconnect supply. If multiple feeds exist, isolate every feed.", "4836:2023 Cl.3.2.1", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "3) Lock + tag", "Apply personal lock and DANGER tag (name/date/reason). Keep key with you to prevent re-energisation.", "4836:2023 Cl.3.2.8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "4) Prove tester", "Test voltage detector on a known live source before dead-testing. Use approved detector only.", "4836:2023 Cl.3.2.6", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "5) Test for dead", "At motor terminals test all relevant combinations: phase-phase, phase-neutral, phase-earth, neutral-earth.", "4836:2023 Cl.3.2.3", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "6) Prove tester again", "Re-test voltage detector on known live source after dead-testing to confirm tester still works.", "4836:2023 Cl.3.2.6", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Disconnect and label", "Disconnect motor conductors and label each wire/terminal (photo or tags) to prevent reconnection errors.", "4836:2023 Sec 3", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Reconnect wiring", "Reconnect wires to correct terminals from manufacturer wiring diagram. Tighten all terminals securely.", "3000:2018 Sec 8; 3017:2022 Sec 4", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Zero continuity leads", "Zero/compensate continuity leads before measuring. Confirm lead-zero value is stable.", "3000:2018 Cl.8.3.5; 3017:2022 Cl.4.4", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Earth continuity check", "Measure continuity from known earth to motor frame. State reading and units clearly.", "3000:2018 Cl.8.3.5; 3017:2022 Cl.4.4", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Insulation resistance check", "If required, perform IR test and record reading with units and pass criterion.", "3000:2018 Cl.8.3.6; 3017:2022 Cl.4.5", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Common mistakes check", "Do not skip prove-live/dead/prove-live. Do not use neon screwdrivers/basic meters. Do not remove tags early.", "4836:2023 Cl.3.2.3, 3.2.6, 3.2.8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Reassemble safely", "Refit covers/guards, check no tools left inside, and confirm terminals are protected.", "4836:2023 Cl.7.2", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Restore supply control", "Warn personnel, remove lock/tag only when safe and authorised, then restore supply.", "4836:2023 Sec 7", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Final motor check", "Did motor start and run correctly with no abnormal heat, sound, smell, or vibration? Answer honestly; No records FAIL.", "4836:2023 Sec 7", RESULT_NONE, NULL, NULL },
};

static const VerifyStep s_swp_appliance[] = {
  { STEP_SAFETY, "Start safe", "Wear PPE, control hazards, and notify nearby people before starting appliance disconnect/reconnect work.", "4836:2023 Sec 2; Sec 11", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Prepare tools", "Confirm approved voltage tester, continuity tester, IR tester, insulated tools, lock, and DANGER tags are ready.", "4836:2023 Sec 8; Sec 9", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Tester battery/check", "Check tester body/leads/fuse for damage and confirm battery/self-test is OK before use.", "4836:2023 Sec 9", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "1) Positive ID", "Identify the correct appliance, all isolation points, and all energy sources feeding the equipment.", "4836:2023 Cl.3.1.2", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "2) Isolate", "Open isolator/circuit breaker to physically disconnect supply. Isolate all feeds where multiple supplies exist.", "4836:2023 Cl.3.2.1", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "3) Lock + tag", "Apply personal lock and DANGER tag (name/date/reason) at isolation point. Keep key in your control.", "4836:2023 Cl.3.2.8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "4) Prove tester", "Test voltage detector on known live source before testing for dead. Use approved detector only.", "4836:2023 Cl.3.2.6", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "5) Test for dead", "At appliance supply points test phase-phase, phase-neutral, phase-earth, neutral-earth as applicable.", "4836:2023 Cl.3.2.3", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "6) Prove tester again", "Re-test voltage detector on known live source after dead-testing to confirm detector health.", "4836:2023 Cl.3.2.6", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Disconnect and label", "Disconnect appliance wiring and label each conductor/terminal before reconnection.", "4836:2023 Sec 3", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Reconnect wiring", "Reconnect as per manufacturer diagram and check terminal tightness.", "3000:2018 Sec 8; 3017:2022 Sec 4", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Zero continuity leads", "Zero/compensate continuity leads before testing earth continuity.", "3000:2018 Cl.8.3.5; 3017:2022 Cl.4.4", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Earth continuity check", "Measure continuity from known earth to appliance frame and state reading with units.", "3000:2018 Cl.8.3.5; 3017:2022 Cl.4.4", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Insulation resistance check", "Run IR test live-to-earth/frame and record reading, units, and pass criterion.", "3000:2018 Cl.8.3.6; 3017:2022 Cl.4.5", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Common mistakes check", "Never trust breaker OFF alone. Never skip prove-live/dead/prove-live. Never remove lock/tag early.", "4836:2023 Cl.3.2.1, 3.2.3, 3.2.6, 3.2.8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Reassemble safely", "Secure covers and cable clamps/strain relief. Confirm no exposed live parts.", "4836:2023 Cl.7.2", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Restore supply control", "Warn personnel, remove lock/tag only when safe and authorised, then restore supply.", "4836:2023 Sec 7", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Final appliance check", "Is appliance operating correctly and safely with no abnormal heat, smell, noise, or faults? Answer honestly; No records FAIL.", "4836:2023 Sec 7", RESULT_NONE, NULL, NULL },
};

static const VerifyStep s_swp_heater_sheathed[] = {
  { STEP_SAFETY, "Start safe", "Wear PPE, control hazards, and notify people before working on heater/sheathed element circuit.", "4836:2023 Sec 2; Sec 11", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Prepare tools", "Confirm approved voltage tester, continuity tester, IR tester, insulated tools, lock, and DANGER tags are ready.", "4836:2023 Sec 8; Sec 9", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Tester battery/check", "Check tester body/leads/fuse for damage and confirm battery/self-test is OK before use.", "4836:2023 Sec 9", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "1) Positive ID", "Identify correct heater/sheathed element, all isolation points, and all energy sources feeding it.", "4836:2023 Cl.3.1.2", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "2) Isolate", "Open isolator/circuit breaker to physically disconnect supply. Isolate all feeds if more than one source.", "4836:2023 Cl.3.2.1", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "3) Lock + tag", "Apply personal lock and DANGER tag (name/date/reason). Keep key with you to prevent re-energisation.", "4836:2023 Cl.3.2.8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "4) Prove tester", "Test voltage detector on known live source before dead-testing. Use approved detector only.", "4836:2023 Cl.3.2.6", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "5) Test for dead", "At terminals test phase-phase, phase-neutral, phase-earth, neutral-earth as applicable.", "4836:2023 Cl.3.2.3", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "6) Prove tester again", "Re-test voltage detector on known live source after dead-testing to verify it still works.", "4836:2023 Cl.3.2.6", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Disconnect and label", "Disconnect element wiring and label wires/terminals before reconnecting.", "4836:2023 Sec 3", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Reconnect wiring", "Reconnect heater/sheathed element wiring to correct terminals and tighten securely.", "3000:2018 Sec 8; 3017:2022 Sec 4", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Zero continuity leads", "Zero/compensate continuity leads before continuity measurement.", "3000:2018 Cl.8.3.5; 3017:2022 Cl.4.4", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Earth continuity check", "Measure continuity from known earth to frame/sheath and state reading with units.", "3000:2018 Cl.8.3.5; 3017:2022 Cl.4.4", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Insulation resistance check", "Perform IR test and record reading with units. For sheathed heating, apply required minimum criterion for this installation.", "3000:2018 Cl.8.3.6; 3017:2022 Cl.4.5", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Common mistakes check", "Do not miss extra energy sources, do not use wrong tester, and do not remove tag before work is complete.", "4836:2023 Cl.3.1.2, 3.2.6, 3.2.8", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Reassemble safely", "Refit all covers and verify no exposed live parts remain.", "4836:2023 Cl.7.2", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Restore supply control", "Warn personnel, remove lock/tag only when safe and authorised, then restore supply.", "4836:2023 Sec 7", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Final heater check", "Does heater/sheathed element operate correctly and safely with no abnormal condition? Answer honestly; No records FAIL.", "4836:2023 Sec 7", RESULT_NONE, NULL, NULL },
};

/** In-service checklist (training aid — use site test sheets for tabulated limits). */
static const VerifyStep s_pat_3760[] = {
  { STEP_SAFETY, "Safety", "Isolate or disconnect equipment per site procedure before inspection. Use appropriate PPE and test instruments.", "3760:2022 Sec 2", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Scope", "This guide aligns with AS/NZS 3760:2022 in-service safety inspection. Your answers set PASS or FAIL on the result screen; use employer forms for tabulated limits.", "3760:2022 Sec 1", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Visual inspection", "Inspect flexible cords, plugs, guards, housings, and labels for damage, overheating signs, and strain.", "3760:2022 Sec 2.4.2", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Visual outcome", "Is the item free from visible damage or defects that would make it unsafe?", "3760:2022 Sec 2.4.2", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Test selection", "Class I earthed items need protective earth continuity where applicable; Class II is double-insulated. Add IR/leakage per your procedure.", "3760:2022 Sec 2.4; 3000:2018 Sec 8", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Electrical tests", "Have required electrical tests been completed with PASS results per your 3760 procedure?", "3760:2022 Sec 2.4", RESULT_NONE, NULL, NULL },
  { STEP_INFO, "Operation", "Where safe, operate the equipment and watch for abnormal heat, noise, smell, or malfunction.", "3760:2022 Sec 2.4", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Operation OK", "Does the item operate normally with no hazardous fault indication?", "3760:2022 Sec 2.4", RESULT_NONE, NULL, NULL },
  { STEP_VERIFY_YESNO, "Records", "Is documentation complete and the item cleared as safe for continued use (tag/record as required)?", "3760:2022 Sec 2", RESULT_NONE, NULL, NULL },
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
  s_swp_heater_sheathed,
  s_pat_3760
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
  sizeof(s_pat_3760) / sizeof(s_pat_3760[0]),
};

int VerificationSteps_getActiveTestCount(void) {
  if (s_customActive && s_customActiveTestCount > 0) return s_customActiveTestCount;
  return kDefaultTestCount;
}

static const char* stepTypeToStr(VerifyStepType t) {
  switch (t) {
    case STEP_SAFETY: return "safety";
    case STEP_VERIFY_YESNO: return "verify_yesno";
    case STEP_RESULT_ENTRY: return "result_entry";
    case STEP_INFO: return "info";
    default: return "info";
  }
}

static bool parseStepType(const char* s, VerifyStepType* out) {
  if (!s || !out) return false;
  if (strcmp(s, "safety") == 0) *out = STEP_SAFETY;
  else if (strcmp(s, "verify_yesno") == 0) *out = STEP_VERIFY_YESNO;
  else if (strcmp(s, "result_entry") == 0) *out = STEP_RESULT_ENTRY;
  else if (strcmp(s, "info") == 0) *out = STEP_INFO;
  else return false;
  return true;
}

static const char* resultKindToStr(VerifyResultKind k) {
  switch (k) {
    case RESULT_CONTINUITY_OHM: return "continuity_ohm";
    case RESULT_IR_MOHM: return "ir_mohm";
    case RESULT_IR_MOHM_SHEATHED: return "ir_mohm_sheathed";
    case RESULT_RCD_REQUIRED_MAX_MS: return "rcd_required_max_ms";
    case RESULT_RCD_MS: return "rcd_ms";
    case RESULT_EFLI_OHM: return "efli_ohm";
    case RESULT_NONE: return "none";
    default: return "none";
  }
}

static bool parseResultKind(const char* s, VerifyResultKind* out) {
  if (!s || !out) return false;
  if (strcmp(s, "continuity_ohm") == 0) *out = RESULT_CONTINUITY_OHM;
  else if (strcmp(s, "ir_mohm") == 0) *out = RESULT_IR_MOHM;
  else if (strcmp(s, "ir_mohm_sheathed") == 0) *out = RESULT_IR_MOHM_SHEATHED;
  else if (strcmp(s, "rcd_required_max_ms") == 0) *out = RESULT_RCD_REQUIRED_MAX_MS;
  else if (strcmp(s, "rcd_ms") == 0) *out = RESULT_RCD_MS;
  else if (strcmp(s, "efli_ohm") == 0) *out = RESULT_EFLI_OHM;
  else if (strcmp(s, "none") == 0) *out = RESULT_NONE;
  else return false;
  return true;
}

static CmpOp parseCmpOp(const char* s) {
  if (!s) return CMP_DEFAULT;
  if (strcmp(s, "<") == 0) return CMP_LT;
  if (strcmp(s, ">") == 0) return CMP_GT;
  if (strcmp(s, "<=") == 0) return CMP_LE;
  if (strcmp(s, ">=") == 0) return CMP_GE;
  if (strcmp(s, "==") == 0) return CMP_EQ;
  if (strcmp(s, "between") == 0) return CMP_BETWEEN;
  return CMP_DEFAULT;
}

static const char* cmpOpToStr(CmpOp op) {
  switch (op) {
    case CMP_LT: return "<";
    case CMP_GT: return ">";
    case CMP_LE: return "<=";
    case CMP_GE: return ">=";
    case CMP_EQ: return "==";
    case CMP_BETWEEN: return "between";
    default: return "";
  }
}

const char* VerificationSteps_getTestName(VerifyTestId id) {
  if (id < 0 || id >= VERIFY_TEST_CAPACITY) return "";
  if (s_customActive) {
    if (!s_customTestNames || id >= s_customActiveTestCount) return "";
    return s_customTestNames[id];
  }
  if (id >= kDefaultTestCount) return "";
  return s_testNames[id];
}

int VerificationSteps_getStepCount(VerifyTestId id) {
  if (id < 0 || id >= VERIFY_TEST_CAPACITY) return 0;
  if (s_customActive) {
    if (!s_customStepCounts || id >= s_customActiveTestCount) return 0;
    return s_customStepCounts[id];
  }
  if (id >= kDefaultTestCount) return 0;
  return s_stepCounts[id];
}

void VerificationSteps_getStep(VerifyTestId id, int stepIndex, VerifyStep* out) {
  if (!out || id < 0 || id >= VERIFY_TEST_CAPACITY) return;
  if (s_customActive) {
    if (!s_customSteps || !s_customStepCounts || id >= s_customActiveTestCount) return;
    if (stepIndex < 0 || stepIndex >= s_customStepCounts[id]) return;
    CustomVerifyStep* cs = &s_customSteps[id][stepIndex];
    out->type = cs->type;
    out->title = cs->title;
    out->instruction = cs->instruction;
    out->clause = cs->clause;
    out->resultKind = cs->resultKind;
    out->resultLabel = cs->resultLabel;
    out->unit = cs->unit;
    return;
  }
  if (stepIndex < 0 || stepIndex >= s_stepCounts[id]) return;
  memcpy(out, &s_steps[id][stepIndex], sizeof(VerifyStep));
}

bool VerificationSteps_expectedYesForStep(VerifyTestId id, int stepIndex) {
  if (id < 0 || id >= VERIFY_TEST_CAPACITY || stepIndex < 0) return true;
  if (s_customActive) {
    if (!s_customExpectedYes || !s_customStepCounts || id >= s_customActiveTestCount) return true;
    if (stepIndex >= s_customStepCounts[id]) return true;
    return s_customExpectedYes[id][stepIndex];
  }
  return true;  // factory defaults: yes/no checks expect "Yes" unless overridden by custom JSON
}

bool VerificationSteps_yesNoStepIsBranchOnly(VerifyTestId id, int stepIndex, const VerifyStep* step) {
  if (!step || step->type != STEP_VERIFY_YESNO || stepIndex < 0) return false;
  if (s_customActive) {
    if (s_customYesNoBranchOnly && s_customStepCounts && id >= 0 && id < s_customActiveTestCount &&
        stepIndex < s_customStepCounts[id] && s_customYesNoBranchOnly[id][stepIndex])
      return true;
  }
  if (id == VERIFY_INSULATION && step->title && strcmp(step->title, "Sheathed heating") == 0) return true;
  if (id == VERIFY_RCD && step->title && strncmp(step->title, "Scenario:", 9) == 0) return true;
  return false;
}

static const char* criterionUnitSuffix(VerifyResultKind k) {
  switch (k) {
    case RESULT_CONTINUITY_OHM:
    case RESULT_EFLI_OHM:
      return " ohm";
    case RESULT_IR_MOHM:
    case RESULT_IR_MOHM_SHEATHED:
      return " MOhm";
    case RESULT_RCD_MS:
    case RESULT_RCD_REQUIRED_MAX_MS:
      return " ms";
    default:
      return "";
  }
}

void VerificationSteps_formatResultCriterion(VerifyResultKind kind, bool isSheathedHeating, float rcdMaxMsOverride, char* buf, unsigned buf_size) {
  if (!buf || buf_size < 8) return;
  buf[0] = '\0';
  if (kind == RESULT_RCD_REQUIRED_MAX_MS || kind == RESULT_NONE) return;

  if (kind == RESULT_RCD_MS && rcdMaxMsOverride > 0.0f) {
    snprintf(buf, buf_size, "Pass if: <= %.4g ms", (double)rcdMaxMsOverride);
    return;
  }

  VerifyResultKind eff = kind;
  if (kind == RESULT_IR_MOHM && isSheathedHeating) eff = RESULT_IR_MOHM_SHEATHED;

  if (eff >= 0 && eff < RESULT_NONE && s_customOps[eff] != CMP_DEFAULT) {
    const char* u = criterionUnitSuffix(eff);
    switch (s_customOps[eff]) {
      case CMP_LT:
        snprintf(buf, buf_size, "Pass if: < %.4g%s", (double)s_customVals[eff], u);
        break;
      case CMP_GT:
        snprintf(buf, buf_size, "Pass if: > %.4g%s", (double)s_customVals[eff], u);
        break;
      case CMP_LE:
        snprintf(buf, buf_size, "Pass if: <= %.4g%s", (double)s_customVals[eff], u);
        break;
      case CMP_GE:
        snprintf(buf, buf_size, "Pass if: >= %.4g%s", (double)s_customVals[eff], u);
        break;
      case CMP_EQ:
        snprintf(buf, buf_size, "Pass if: = %.4g%s", (double)s_customVals[eff], u);
        break;
      case CMP_BETWEEN: {
        float lo = s_customVals[eff];
        float hi = s_customValsMax[eff];
        if (hi < lo) {
          float t = lo;
          lo = hi;
          hi = t;
        }
        snprintf(buf, buf_size, "Pass if: %.4g – %.4g%s", (double)lo, (double)hi, u);
        break;
      }
      default:
        break;
    }
    if (buf[0]) return;
  }

  if (kind == RESULT_IR_MOHM && isSheathedHeating) {
    snprintf(buf, buf_size, "Pass if: >= %.4g MOhm", (double)TestLimits_insulationMinMOhmsSheathedHeating());
    return;
  }
  switch (kind) {
    case RESULT_CONTINUITY_OHM:
      snprintf(buf, buf_size, "Pass if: <= %.4g ohm", (double)TestLimits_continuityMaxOhms());
      break;
    case RESULT_IR_MOHM:
      snprintf(buf, buf_size, "Pass if: >= %.4g MOhm", (double)TestLimits_insulationMinMOhms());
      break;
    case RESULT_IR_MOHM_SHEATHED:
      snprintf(buf, buf_size, "Pass if: >= %.4g MOhm", (double)TestLimits_insulationMinMOhmsSheathedHeating());
      break;
    case RESULT_EFLI_OHM:
      snprintf(buf, buf_size, "Pass if: <= %.4g ohm", (double)TestLimits_efliMaxOhms());
      break;
    case RESULT_RCD_MS:
      snprintf(buf, buf_size, "Pass if: <= %.4g ms", (double)TestLimits_rcdTripTimeMaxMs());
      break;
    default:
      break;
  }
}

bool VerificationSteps_validateResult(VerifyResultKind kind, float value, bool isSheathedHeating) {
  if (kind >= 0 && kind < RESULT_NONE && s_customOps[kind] != CMP_DEFAULT) {
    switch (s_customOps[kind]) {
      case CMP_LT: return value < s_customVals[kind];
      case CMP_GT: return value > s_customVals[kind];
      case CMP_LE: return value <= s_customVals[kind];
      case CMP_GE: return value >= s_customVals[kind];
      case CMP_EQ: return value == s_customVals[kind];
      case CMP_BETWEEN: {
        float lo = s_customVals[kind];
        float hi = s_customValsMax[kind];
        if (hi < lo) { float t = lo; lo = hi; hi = t; }
        return value >= lo && value <= hi;
      }
      default: break;
    }
  }
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

void VerificationSteps_appendRulesJsonArray(JsonArray rules) {
  for (int k = 0; k < RESULT_NONE; k++) {
    VerifyResultKind kind = (VerifyResultKind)k;
    if (s_customOps[k] != CMP_DEFAULT) {
      JsonObject r = rules.createNestedObject();
      r["kind"] = resultKindToStr(kind);
      r["op"] = cmpOpToStr(s_customOps[k]);
      if (s_customOps[k] == CMP_BETWEEN) {
        r["min"] = s_customVals[k];
        r["max"] = s_customValsMax[k];
      } else {
        r["value"] = s_customVals[k];
      }
      continue;
    }
    /* Factory defaults: same limits as AdminPortal ensureDefaultRulesInDoc / TestLimits. */
    switch (kind) {
      case RESULT_CONTINUITY_OHM: {
        JsonObject r = rules.createNestedObject();
        r["kind"] = resultKindToStr(kind);
        r["op"] = "<=";
        r["value"] = TestLimits_continuityMaxOhms();
        break;
      }
      case RESULT_IR_MOHM: {
        JsonObject r = rules.createNestedObject();
        r["kind"] = resultKindToStr(kind);
        r["op"] = ">=";
        r["value"] = TestLimits_insulationMinMOhms();
        break;
      }
      case RESULT_IR_MOHM_SHEATHED: {
        JsonObject r = rules.createNestedObject();
        r["kind"] = resultKindToStr(kind);
        r["op"] = ">=";
        r["value"] = TestLimits_insulationMinMOhmsSheathedHeating();
        break;
      }
      case RESULT_EFLI_OHM: {
        JsonObject r = rules.createNestedObject();
        r["kind"] = resultKindToStr(kind);
        r["op"] = "<=";
        r["value"] = TestLimits_efliMaxOhms();
        break;
      }
      case RESULT_RCD_REQUIRED_MAX_MS: {
        JsonObject r = rules.createNestedObject();
        r["kind"] = resultKindToStr(kind);
        r["op"] = "<=";
        r["value"] = TestLimits_rcdTripTimeMaxMs();
        break;
      }
      case RESULT_RCD_MS: {
        JsonObject r = rules.createNestedObject();
        r["kind"] = resultKindToStr(kind);
        r["op"] = "<=";
        r["value"] = TestLimits_rcdTripTimeMaxMs();
        break;
      }
      default:
        break;
    }
  }
}

bool VerificationSteps_getConfigJson(char* buf, unsigned buf_size) {
  if (!buf || buf_size < 64) return false;
  DynamicJsonDocument doc(524288);
  JsonArray tests = doc.createNestedArray("tests");
  for (int i = 0; i < VerificationSteps_getActiveTestCount(); i++) {
    JsonObject t = tests.createNestedObject();
    t["id"] = i;
    t["name"] = VerificationSteps_getTestName((VerifyTestId)i);
    JsonArray steps = t.createNestedArray("steps");
    int count = VerificationSteps_getStepCount((VerifyTestId)i);
    for (int s = 0; s < count; s++) {
      VerifyStep st{};
      VerificationSteps_getStep((VerifyTestId)i, s, &st);
      JsonObject so = steps.createNestedObject();
      so["type"] = stepTypeToStr(st.type);
      so["title"] = st.title ? st.title : "";
      so["instruction"] = st.instruction ? st.instruction : "";
      so["clause"] = st.clause ? st.clause : "";
      so["resultKind"] = resultKindToStr(st.resultKind);
      so["resultLabel"] = st.resultLabel ? st.resultLabel : "";
      so["unit"] = st.unit ? st.unit : "";
      if (st.type == STEP_VERIFY_YESNO) {
        if (VerificationSteps_yesNoStepIsBranchOnly((VerifyTestId)i, s, &st))
          so["expectedYesNo"] = "branch";
        else
          so["expectedYesNo"] = VerificationSteps_expectedYesForStep((VerifyTestId)i, s) ? "yes" : "no";
      }
    }
  }
  JsonArray rules = doc.createNestedArray("rules");
  VerificationSteps_appendRulesJsonArray(rules);
  size_t n = serializeJsonPretty(doc, buf, buf_size);
  return n > 0 && n < buf_size;
}

bool VerificationSteps_activateConfigJson(const char* json, char* err, unsigned err_size) {
  if (err && err_size) err[0] = '\0';
  if (!json || !json[0]) {
    if (err && err_size) snprintf(err, err_size, "JSON is empty");
    return false;
  }
  /* Large embedded tests.json (many steps) needs a big pool; keep in sync with AdminPortal kTestsJsonDocCap. */
  DynamicJsonDocument doc(720896);
  auto de = deserializeJson(doc, json);
  if (de) {
    if (err && err_size) snprintf(err, err_size, "JSON parse error: %s", de.c_str());
    return false;
  }
  JsonArray tests = doc["tests"].as<JsonArray>();
  if (tests.isNull()) {
    if (err && err_size) snprintf(err, err_size, "Missing tests[]");
    return false;
  }
  std::unique_ptr<TestNameBuf[]> nextNames(new TestNameBuf[VERIFY_TEST_CAPACITY]());
  std::unique_ptr<TestStepsBuf[]> nextSteps(new TestStepsBuf[VERIFY_TEST_CAPACITY]());
  std::unique_ptr<TestExpectedBuf[]> nextExpectedYes(new TestExpectedBuf[VERIFY_TEST_CAPACITY]());
  std::unique_ptr<TestExpectedBuf[]> nextYesNoBranch(new TestExpectedBuf[VERIFY_TEST_CAPACITY]());
  std::unique_ptr<int[]> nextCounts(new int[VERIFY_TEST_CAPACITY]());
  if (!nextNames || !nextSteps || !nextExpectedYes || !nextYesNoBranch || !nextCounts) {
    if (err && err_size) snprintf(err, err_size, "Out of memory");
    return false;
  }
  CmpOp nextOps[RESULT_NONE] = {};
  float nextVals[RESULT_NONE] = {};
  float nextValsMax[RESULT_NONE] = {};
  for (int i = 0; i < RESULT_NONE; i++) {
    nextOps[i] = CMP_DEFAULT;
    nextVals[i] = 0.0f;
    nextValsMax[i] = 0.0f;
  }

  int autoId = 0;
  int maxId = -1;
  for (JsonObject t : tests) {
    int id = t["id"] | autoId;
    autoId++;
    if (id < 0 || id >= VERIFY_TEST_CAPACITY) continue;
    const char* nm = t["name"] | "";
    strncpy(nextNames[id], nm, VERIFY_MAX_NAME_LEN - 1);
    JsonArray steps = t["steps"].as<JsonArray>();
    if (steps.isNull()) {
      if (err && err_size) snprintf(err, err_size, "Test %d missing steps", id);
      return false;
    }
    int idx = 0;
    for (JsonObject so : steps) {
      if (idx >= VERIFY_MAX_STEPS_PER_TEST) break;
      VerifyStepType stype;
      VerifyResultKind rkind;
      if (!parseStepType(so["type"] | "", &stype)) {
        if (err && err_size) snprintf(err, err_size, "Invalid step type in test %d", id);
        return false;
      }
      if (!parseResultKind(so["resultKind"] | "none", &rkind)) {
        if (err && err_size) snprintf(err, err_size, "Invalid resultKind in test %d", id);
        return false;
      }
      if (rkind == RESULT_RCD_REQUIRED_MAX_MS) {
        /* Trip limit comes from RCD scenario answers only; ignore legacy manual-max steps in JSON. */
        continue;
      }
      CustomVerifyStep* cs = &nextSteps[id][idx++];
      cs->type = stype;
      bool expectedYes = true;
      bool yesNoBranch = false;
      if (stype == STEP_VERIFY_YESNO) {
        const char* exp = so["expectedYesNo"] | "yes";
        if (strcmp(exp, "branch") == 0) {
          yesNoBranch = true;
          expectedYes = true;
        } else if (strcmp(exp, "no") == 0 || strcmp(exp, "false") == 0 || strcmp(exp, "0") == 0)
          expectedYes = false;
      }
      nextExpectedYes[id][idx - 1] = expectedYes;
      nextYesNoBranch[id][idx - 1] = yesNoBranch;
      cs->resultKind = rkind;
      strncpy(cs->title, so["title"] | "", VERIFY_MAX_TITLE_LEN - 1);
      strncpy(cs->instruction, so["instruction"] | "", VERIFY_MAX_INSTR_LEN - 1);
      strncpy(cs->clause, so["clause"] | "", VERIFY_MAX_CLAUSE_LEN - 1);
      strncpy(cs->resultLabel, so["resultLabel"] | "", VERIFY_MAX_LABEL_LEN - 1);
      strncpy(cs->unit, so["unit"] | "", VERIFY_MAX_UNIT_LEN - 1);
    }
    if (idx <= 0) {
      if (err && err_size) snprintf(err, err_size, "Test %d has no steps", id);
      return false;
    }
    nextCounts[id] = idx;
    if (id > maxId) maxId = id;
  }
  if (maxId < 0) {
    if (err && err_size) snprintf(err, err_size, "No tests provided");
    return false;
  }
  for (int i = 0; i <= maxId; i++) {
    if (nextCounts[i] <= 0) {
      if (err && err_size) snprintf(err, err_size, "Missing test id %d", i);
      return false;
    }
    if (!nextNames[i][0]) {
      if (i < kDefaultTestCount) strncpy(nextNames[i], s_testNames[i], VERIFY_MAX_NAME_LEN - 1);
      else snprintf(nextNames[i], VERIFY_MAX_NAME_LEN, "Custom test %d", i + 1);
    }
  }
  JsonArray rules = doc["rules"].as<JsonArray>();
  if (!rules.isNull()) {
    for (JsonObject r : rules) {
      VerifyResultKind kind;
      if (!parseResultKind(r["kind"] | "", &kind) || kind == RESULT_NONE) continue;
      CmpOp op = parseCmpOp(r["op"] | "");
      if (op == CMP_DEFAULT) {
        if (err && err_size) snprintf(err, err_size, "Invalid comparator op");
        return false;
      }
      nextOps[kind] = op;
      if (op == CMP_BETWEEN) {
        if (!r.containsKey("min") || !r.containsKey("max")) {
          if (err && err_size) snprintf(err, err_size, "between comparator requires min and max");
          return false;
        }
        nextVals[kind] = r["min"] | 0.0f;
        nextValsMax[kind] = r["max"] | 0.0f;
      } else {
        nextVals[kind] = r["value"] | 0.0f;
        nextValsMax[kind] = 0.0f;
      }
    }
  }

  s_customTestNames = std::move(nextNames);
  s_customSteps = std::move(nextSteps);
  s_customExpectedYes = std::move(nextExpectedYes);
  s_customYesNoBranchOnly = std::move(nextYesNoBranch);
  s_customStepCounts = std::move(nextCounts);
  s_customActiveTestCount = maxId + 1;
  memcpy(s_customOps, nextOps, sizeof(s_customOps));
  memcpy(s_customVals, nextVals, sizeof(s_customVals));
  memcpy(s_customValsMax, nextValsMax, sizeof(s_customValsMax));
  s_customActive = true;
  return true;
}

void VerificationSteps_useFactoryDefaults(void) {
  s_customActive = false;
  s_customActiveTestCount = 0;
  s_customTestNames.reset();
  s_customSteps.reset();
  s_customExpectedYes.reset();
  s_customYesNoBranchOnly.reset();
  s_customStepCounts.reset();
  for (int i = 0; i < RESULT_NONE; i++) {
    s_customOps[i] = CMP_DEFAULT;
    s_customVals[i] = 0.0f;
    s_customValsMax[i] = 0.0f;
  }
}

const char* VerificationSteps_getClauseForResult(VerifyResultKind kind) {
  switch (kind) {
    case RESULT_CONTINUITY_OHM: return "AS/NZS 3000:2018 Cl.8.3.5; AS/NZS 3017:2022 Cl.4.4";
    case RESULT_IR_MOHM:
    case RESULT_IR_MOHM_SHEATHED: return "AS/NZS 3000:2018 Cl.8.3.6; AS/NZS 3017:2022 Cl.4.5";
    case RESULT_RCD_REQUIRED_MAX_MS: return "AS/NZS 3000:2018 Cl.2.6; AS/NZS 3017:2022 Cl.4.9.5.3";
    case RESULT_RCD_MS: return "AS/NZS 3000:2018 Cl.8.3.10; AS/NZS 3017:2022 Cl.4.9.5.3";
    case RESULT_EFLI_OHM: return "AS/NZS 3000:2018 Cl.8.3.9; AS/NZS 3017:2022 Cl.4.8";
    default: return "";
  }
}
