/**
 * SparkyCheck – First boot screen (creator credit)
 * Displays product name, "Created by Frank", and industry-themed graphic.
 */

#pragma once

#include <TFT_eSPI.h>

namespace BootScreen {

/** Show the first boot screen (creator + graphic). Blocks until tap or timeout. */
void showFirst(TFT_eSPI& tft);

/** Show disclaimer screen. Blocks until user taps "I Accept". */
void showDisclaimer(TFT_eSPI& tft);

/** Draw the industry-themed graphic (checkmark, cable, verification motif). */
void drawIndustryGraphic(TFT_eSPI& tft, int centerX, int centerY, int size);

/** True when a touch point is on the creator-credit text region. */
bool isCreatorCreditTouchRegion(TFT_eSPI& tft, int x, int y);

}  // namespace BootScreen
