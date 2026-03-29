#include "SparkyTzPresets.h"

#include <stdio.h>

void SparkyTzPresets_formatUtcOffset(int16_t offsetMinutes, char* buf, unsigned bufSize) {
  if (!buf || bufSize < 14) return;
  int neg = offsetMinutes < 0;
  int32_t m = neg ? -(int32_t)offsetMinutes : (int32_t)offsetMinutes;
  int32_t h = m / 60;
  int32_t r = m % 60;
  const char* sgn = neg ? "-" : "+";
  if (r == 0)
    snprintf(buf, bufSize, "UTC%s%ld", sgn, (long)h);
  else if (r == 30)
    snprintf(buf, bufSize, "UTC%s%ld:30", sgn, (long)h);
  else if (r == 45)
    snprintf(buf, bufSize, "UTC%s%ld:45", sgn, (long)h);
  else
    snprintf(buf, bufSize, "UTC%s%ld:%02ld", sgn, (long)h, (long)r);
  buf[bufSize - 1] = '\0';
}

/* AU/NZ city-oriented presets (standard-time minutes east of UTC).
 * Use Extra +1 h when your state observes daylight saving (not QLD, NT, or WA). */
static const SparkyTzPreset kTable[] = {
    {"Perth", 480},
    {"Darwin, Adelaide", 570},
    {"Sydney, Melbourne, Brisbane, Hobart, Canberra", 600},
    {"Lord Howe Island", 630},
    {"Auckland, Wellington, Christchurch", 720},
    {"Chatham Islands", 765},
};

unsigned SparkyTzPresets_count(void) {
  return (unsigned)(sizeof(kTable) / sizeof(kTable[0]));
}

const SparkyTzPreset* SparkyTzPresets_get(unsigned index) {
  if (index >= SparkyTzPresets_count()) return nullptr;
  return &kTable[index];
}

unsigned SparkyTzPresets_indexForOffset(int16_t offsetMinutes) {
  const unsigned n = SparkyTzPresets_count();
  for (unsigned i = 0; i < n; i++) {
    if (kTable[i].offsetMinutes == offsetMinutes) return i;
  }
  return n;
}
