/**
 * AU/NZ city-based timezone presets (standard-time offset, minutes east of UTC).
 * Daylight saving: use AppState "Extra +1 h" where your state uses it (not QLD, NT, WA).
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char* label;
  int16_t offsetMinutes;
} SparkyTzPreset;

unsigned SparkyTzPresets_count(void);
const SparkyTzPreset* SparkyTzPresets_get(unsigned index);

/** First index whose offset matches, or SparkyTzPresets_count() if none. */
unsigned SparkyTzPresets_indexForOffset(int16_t offsetMinutes);

/** Human-readable offset, e.g. "UTC+10", "UTC+9:30", "UTC+12:45", "UTC-5". */
void SparkyTzPresets_formatUtcOffset(int16_t offsetMinutes, char* buf, unsigned bufSize);

#ifdef __cplusplus
}
#endif
