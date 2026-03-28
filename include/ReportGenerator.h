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

/**
 * Start a new report. If job_id_or_null is non-empty, it is used as the basename (advanced).
 * Otherwise the basename is {device/cubicle}_{TestSlug} with optional trailing digit if that
 * name already exists (e.g. CUB01_EarthContinuity, then CUB01_EarthContinuity1, …).
 * test_name_or_null should be the verification test display name (from VerificationSteps).
 */
bool ReportGenerator_begin(const char* job_id_or_null, const char* test_name_or_null);

/** Add a result line. */
void ReportGenerator_addResult(const char* name, const char* value, const char* unit, bool passed, const char* clause);

/** Set student ID metadata for next report (optional). */
void ReportGenerator_setStudentId(const char* student_id);

/** Finalise and write CSV + HTML. Returns false on write error. */
bool ReportGenerator_end(void);

/** Get last generated basename (e.g. "CUB01_EarthContinuity") for display. */
void ReportGenerator_getLastBasename(char* buf, unsigned buf_size);

/** List report basenames into buf (newline-sep). Returns count. */
int ReportGenerator_listReports(char* buf, unsigned buf_size);

/**
 * Fill up to maxLines basenames (no extension), newest-friendly order (descending name).
 * Returns number of entries (0..maxLines).
 */
int ReportGenerator_fillBasenameList(char (*lines)[64], int maxLines);

/** Remove both /reports/<base>.csv and .html. basename must not contain path separators. */
bool ReportGenerator_deleteReportBasename(const char* basename);

/**
 * Read /reports/<basename>.csv into buf (NUL-terminated, truncated to buf_size-1).
 * Returns false if missing or invalid basename.
 */
bool ReportGenerator_readReportCsv(const char* basename, char* buf, unsigned buf_size, unsigned* out_bytes_or_null);

/** Mount filesystem (call once at startup). */
bool ReportGenerator_init(void);

/** Start deferred report email task if pending (call each loop(); not from touch handlers). */
void ReportGenerator_pollDeferredEmail(void);

#ifdef __cplusplus
}
#endif
