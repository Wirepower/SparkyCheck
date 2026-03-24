/**
 * SparkyCheck – First boot screen (creator credit)
 * Displays product name, "Created by Frank", and industry-themed graphic.
 */

#pragma once

#include "SparkyDisplay.h"

namespace BootScreen {

/** Show the first boot screen (creator + graphic). Blocks until tap or timeout. */
void showFirst(SparkyTft& tft);

/** Show disclaimer screen. Blocks until user taps "I Accept". */
void showDisclaimer(SparkyTft& tft);

/** Draw the industry-themed graphic (checkmark, cable, verification motif). */
void drawIndustryGraphic(SparkyTft& tft, int centerX, int centerY, int size);

/** True when a touch point is on the creator-credit text region. */
bool isCreatorCreditTouchRegion(SparkyTft& tft, int x, int y);

}  // namespace BootScreen
