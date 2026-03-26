/**
 * SparkyCheck – First boot screen (splash + disclaimer)
 * Product title, verification motif, minimal credit, tap to continue.
 */

#pragma once

#include "SparkyDisplay.h"

namespace BootScreen {

/** Show the first boot screen (creator + graphic). Blocks until tap or timeout. */
void showFirst(SparkyTft& tft);

/** Show disclaimer screen. Blocks until user taps "I Accept". */
void showDisclaimer(SparkyTft& tft);

/**
 * Boot mark: pure R/W/B phases, black N, safety-green tick, glowing green circle frame.
 * @param innerBg unused (kept for API stability).
 */
void drawIndustryGraphic(SparkyTft& tft, int centerX, int centerY, int size, uint16_t innerBg);

/** True when a touch point is on the boot industry graphic (logo) region. */
bool isBootLogoTouchRegion(SparkyTft& tft, int x, int y);

}  // namespace BootScreen
