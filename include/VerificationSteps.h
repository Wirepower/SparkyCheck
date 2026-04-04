/**
 * SparkyCheck – Verification coach steps from AS/NZS 3000:2018 Sec 8, AS/NZS 3017:2022,
 * AS/NZS 4836:2023 (SWP), and optional AS/NZS 3760:2022 checklist.
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
  VERIFY_IN_SERVICE_3760,
  VERIFY_TEST_COUNT
} VerifyTestId;

/** Maximum custom test slots supported by runtime config. */
#define VERIFY_TEST_CAPACITY 24

/** Max steps stored per test in runtime config (custom JSON and buffers). */
#define VERIFY_MAX_STEPS_PER_TEST 30

/** ArduinoJson parse pool for tests.json (nested tree needs more RAM than raw JSON text). */
#define VERIFICATION_STEPS_JSON_DOC_CAP 1048576

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

/** Expected answer for yes/no steps (true=Yes, false=No). Defaults to Yes. */
bool VerificationSteps_expectedYesForStep(VerifyTestId id, int stepIndex);

/**
 * Yes/No steps that only choose a branch (IR sheathed vs general wiring; RCD scenario)
 * must never record PASS/FAIL. Factory tests use titles; custom JSON may set
 * expectedYesNo to "branch".
 */
bool VerificationSteps_yesNoStepIsBranchOnly(VerifyTestId id, int stepIndex, const VerifyStep* step);

/** True for factory SWP disconnect/reconnect motor, appliance, or heater guides. */
bool VerificationSteps_isSwpFactoryTest(VerifyTestId id);

/**
 * Factory SWP tests share one layout: 0=safety, 1=What are you doing? (role pick), 2=Disconnect-only?, … disconnect-and-label,
 * then reconnect wiring. Indices are defined beside s_swp_motor[] in VerificationSteps.cpp.
 */
int VerificationSteps_getSwpFactoryReconnectStartStep(VerifyTestId id);
int VerificationSteps_getSwpFactoryDisconnectEndStep(VerifyTestId id);

/** Validate entered value for a result step. Returns true if pass. */
bool VerificationSteps_validateResult(VerifyResultKind kind, float value, bool isSheathedHeating);

/** Get pass/fail clause ref for report. */
const char* VerificationSteps_getClauseForResult(VerifyResultKind kind);

/**
 * One-line pass rule for the measurement screen (active Admin rules or factory defaults).
 * For RESULT_RCD_MS, pass rcdMaxMsOverride > 0 to show that limit (scenario-computed).
 * Leaves buf empty for RESULT_RCD_REQUIRED_MAX_MS and unknown kinds.
 */
void VerificationSteps_formatResultCriterion(VerifyResultKind kind, bool isSheathedHeating, float rcdMaxMsOverride, char* buf, unsigned buf_size);

/** Export current test config as JSON for admin editing. */
bool VerificationSteps_getConfigJson(char* buf, unsigned buf_size);

/** Validate + activate test config JSON at runtime. */
bool VerificationSteps_activateConfigJson(const char* json, char* err, unsigned err_size);

/** Disable custom override and use compiled-in factory defaults. */
void VerificationSteps_useFactoryDefaults(void);

/** True when no custom tests.json override is active (factory step order applies). */
bool VerificationSteps_isFactoryDefaultsActive(void);

#ifdef __cplusplus
}

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

/**
 * Large tests.json trees (~1 MiB pool) must not use DynamicJsonDocument (malloc → internal DRAM).
 * After LVGL/WiFi/admin, only ~100 KiB DRAM may be free; PSRAM holds the pool instead.
 */
struct SpiRamJsonAllocator {
  void* allocate(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = malloc(size);
    return p;
  }
  void deallocate(void* ptr) {
    if (ptr) heap_caps_free(ptr);
  }
  void* reallocate(void* ptr, size_t new_size) {
    if (!ptr) return allocate(new_size);
    void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = realloc(ptr, new_size);
    return p;
  }
};

using VerificationJsonDocument = BasicJsonDocument<SpiRamJsonAllocator>;

/** Fill `rules` with comparator rules for export (custom overrides + firmware defaults). */
void VerificationSteps_appendRulesJsonArray(JsonArray rules);

#endif
