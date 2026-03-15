/**
 * SparkyCheck – Versioned standard definitions
 *
 * When AS/NZS standards are updated, change the edition/year and any limits
 * or clause references here (or later: load from config file / OTA).
 */

#pragma once

#include "Standards.h"

#ifdef __cplusplus

#include <cstdint>

namespace SparkyCheck {

/** Current edition strings – update when standards are revised. */
struct StandardEditions {
  static constexpr const char* AS_NZS_3000 = "AS/NZS 3000:2018";
  static constexpr const char* AS_NZS_3017 = "AS/NZS 3017:2022";
  static constexpr const char* AS_NZS_3760 = "AS/NZS 3760:2022";
};

/** Pass/fail limits and clause refs can be added here or in a separate table. */
// e.g. continuity max 0.5 ohm, IR min 1 MΩ, RCD trip times, etc.

}  // namespace SparkyCheck

#endif
