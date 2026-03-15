/**
 * SparkyCheck – Standards configuration implementation
 *
 * Single place for standard editions and mode behaviour. Update these when
 * AS/NZS 3000, 3017, or 3760 are revised.
 */

#include "StandardsConfig.h"
#include "Standards.h"

static bool s_field_mode = false;

void Standards_setFieldMode(bool field_mode) {
  s_field_mode = field_mode;
}

bool Standards_isActiveInCurrentMode(StandardId id) {
  switch (id) {
    case STANDARD_3000:
    case STANDARD_3017:
      return true;  /* Active in both Training and Field */
    case STANDARD_3760:
      return s_field_mode;  /* In-service testing only in Field mode */
    default:
      return false;
  }
}

void Standards_getInfo(StandardId id, StandardInfo* out) {
  if (!out) return;
  switch (id) {
    case STANDARD_3000:
      out->short_name = SparkyCheck::StandardEditions::AS_NZS_3000;
      out->section   = "Section 8 (Verification & Testing)";
      out->title     = "Electrical installations – Wiring Rules";
      break;
    case STANDARD_3017:
      out->short_name = SparkyCheck::StandardEditions::AS_NZS_3017;
      out->section   = "";
      out->title     = "Electrical installations – Verification guidelines";
      break;
    case STANDARD_3760:
      out->short_name = SparkyCheck::StandardEditions::AS_NZS_3760;
      out->section   = "";
      out->title     = "In-service safety inspection and testing of electrical equipment";
      break;
    default:
      out->short_name = "";
      out->section   = "";
      out->title     = "";
      break;
  }
}

void Standards_getRulesVersion(char* buf, unsigned buf_size) {
  if (!buf || buf_size == 0) return;
  /* Bundle version for reports; can be replaced by OTA or file later */
  const char* v = "2026-03";
  unsigned i = 0;
  while (i < buf_size - 1 && v[i]) {
    buf[i] = v[i];
    i++;
  }
  buf[i] = '\0';
}
