/**
 * SparkyCheck – Report generation (CSV + HTML) to internal storage.
 * Uses LittleFS; path prefix /reports/. SD card can be added later.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Max length of a result line (test name + value + pass/fail). */
#define REPORT_MAX_RESULTS 24

/** Single test result for a report. */
typedef struct {
  const char* name;      /* e.g. "Continuity" */
  const char* value;     /* e.g. "0.35" */
  const char* unit;      /* e.g. "ohm" */
  bool passed;           /* pass/fail */
  const char* clause;    /* optional clause ref */
} ReportResult;

/** Start a new report (generates timestamped basename). */
bool ReportGenerator_begin(const char* job_id_or_null);

/** Add a result line. */
void ReportGenerator_addResult(const char* name, const char* value, const char* unit, bool passed, const char* clause);

/** Finalise and write CSV + HTML. Returns false on write error. */
bool ReportGenerator_end(void);

/** Get last generated basename (e.g. "Job_2026-03-15_1430") for display. */
void ReportGenerator_getLastBasename(char* buf, unsigned buf_size);

/** List report basenames into buf (newline-sep). Returns count. */
int ReportGenerator_listReports(char* buf, unsigned buf_size);

/** Mount filesystem (call once at startup). */
bool ReportGenerator_init(void);

#ifdef __cplusplus
}
#endif
