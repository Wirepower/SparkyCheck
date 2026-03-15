/**
 * SparkyCheck – Standards & regulations abstraction
 *
 * AS/NZS standards (e.g. 3000, 3017, 3760) can be revised periodically. This
 * layer keeps standard identifiers, editions, and rule data in one place so
 * that when a new edition is published you can:
 *   • Update version strings and limits in StandardsConfig (or load from
 *     SD/SPIFFS/OTA later) without changing app logic.
 *   • Report which standard edition was used for each verification (audit trail).
 *
 * HOW TO UPDATE WHEN REGULATIONS CHANGE
 * --------------------------------------
 * 1. Update the version/edition in StandardsConfig (e.g. 3000:2018 → 3000:2024).
 * 2. Adjust any pass/fail limits, clause references, or test sequences in
 *    StandardsConfig to match the new standard.
 * 3. (Future) Replace or extend with config files on SD card or OTA-delivered
 *    rule sets so updates don’t require a full firmware recompile.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Recognised standard identifiers (extend when new standards are added). */
typedef enum {
  STANDARD_3000 = 0,  /**< AS/NZS 3000 – Wiring Rules (Section 8 Verification & Testing) */
  STANDARD_3017,      /**< AS/NZS 3017 – Verification guidelines */
  STANDARD_3760,      /**< AS/NZS 3760 – In-service inspection & testing (Field mode) */
  STANDARD_COUNT
} StandardId;

/** Human-readable standard info (edition/year, title). */
typedef struct {
  const char* short_name;   /**< e.g. "AS/NZS 3000:2018" */
  const char* section;      /**< e.g. "Section 8 (Verification & Testing)" or "" */
  const char* title;        /**< e.g. "Electrical installations – Wiring Rules" */
} StandardInfo;

/** Get the current info for a standard (edition, section, title). */
void Standards_getInfo(StandardId id, StandardInfo* out);

/** Return whether this standard is active in the current mode (Training vs Field). */
bool Standards_isActiveInCurrentMode(StandardId id);

/** Set operational mode so 3760 can be enabled only in Field mode. */
void Standards_setFieldMode(bool field_mode);

/** Optional: get a version string for reports (e.g. "2026-03" for bundled rules). */
void Standards_getRulesVersion(char* buf, unsigned buf_size);

#ifdef __cplusplus
}
#endif
