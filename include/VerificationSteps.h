/**
 * SparkyCheck – Verification coach steps from AS/NZS 3000 Section 8 & AS/NZS 3017.
 * Each test type has a sequence of steps; user cannot skip safety or verification steps.
 * Step content and clause refs are in VerificationSteps.cpp; update there (or via OTA
 * config) when standards are revised – see docs/STANDARDS_UPDATE.md.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Test types – user selects one to start the guide. */
typedef enum {
  VERIFY_CONTINUITY = 0,
  VERIFY_INSULATION,
  VERIFY_POLARITY,
  VERIFY_EARTH_CONTINUITY,
  VERIFY_CIRCUIT_CONNECTIONS,
  VERIFY_EFLI,
  VERIFY_RCD,
  VERIFY_SWP_DISCONNECT_RECONNECT_MOTOR,
  VERIFY_SWP_DISCONNECT_RECONNECT_APPLIANCE,
  VERIFY_SWP_DISCONNECT_RECONNECT_HEATER_SHEATHED,
  VERIFY_TEST_COUNT
} VerifyTestId;

/** Maximum custom test slots supported by runtime config. */
#define VERIFY_TEST_CAPACITY 24

/** Step type – determines UI (safety ack, Yes/No, result entry, or OK). */
typedef enum {
  STEP_SAFETY,       /* Must acknowledge (e.g. "I have zeroed") */
  STEP_VERIFY_YESNO, /* Yes / No */
  STEP_RESULT_ENTRY, /* Enter value, then validate pass/fail */
  STEP_INFO          /* Informational, OK to continue */
} VerifyStepType;

/** Result kind for STEP_RESULT_ENTRY – used for validation and report. */
typedef enum {
  RESULT_CONTINUITY_OHM,
  RESULT_IR_MOHM,
  RESULT_IR_MOHM_SHEATHED,
  RESULT_RCD_REQUIRED_MAX_MS,
  RESULT_RCD_MS,
  RESULT_EFLI_OHM,
  RESULT_NONE
} VerifyResultKind;

typedef struct {
  VerifyStepType type;
  const char* title;
  const char* instruction;
  const char* clause;        /* e.g. "AS/NZS 3000 Clause 8.3.5" */
  VerifyResultKind resultKind;
  const char* resultLabel;    /* e.g. "Continuity" */
  const char* unit;          /* e.g. "ohm", "MOhm", "ms" */
} VerifyStep;

/** Get short name for test (for selection list and report). */
const char* VerificationSteps_getTestName(VerifyTestId id);

/** Get number of steps for this test. */
int VerificationSteps_getStepCount(VerifyTestId id);

/** Number of active tests currently exposed to UI flow. */
int VerificationSteps_getActiveTestCount(void);

/** Get step at index (0 .. count-1). */
void VerificationSteps_getStep(VerifyTestId id, int stepIndex, VerifyStep* out);

/** Validate entered value for a result step. Returns true if pass. */
bool VerificationSteps_validateResult(VerifyResultKind kind, float value, bool isSheathedHeating);

/** Get pass/fail clause ref for report. */
const char* VerificationSteps_getClauseForResult(VerifyResultKind kind);

/** Export current test config as JSON for admin editing. */
bool VerificationSteps_getConfigJson(char* buf, unsigned buf_size);

/** Validate + activate test config JSON at runtime. */
bool VerificationSteps_activateConfigJson(const char* json, char* err, unsigned err_size);

/** Disable custom override and use compiled-in factory defaults. */
void VerificationSteps_useFactoryDefaults(void);

#ifdef __cplusplus
}
#endif
