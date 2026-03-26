#pragma once

#include <stdint.h>

#include "Screens.h"
#include "SparkyDisplay.h"

/**
 * Draw and handle a testable on-device UI generated from EEZ mockup data.
 *
 * This runtime intentionally falls back to the existing Screens.cpp engine for
 * complex input screens so application behavior remains aligned with master.
 */
void EezMockupUi_draw(SparkyTft* tft, ScreenId id);
ScreenId EezMockupUi_handleTouch(SparkyTft* tft, ScreenId current, uint16_t x, uint16_t y);
bool EezMockupUi_didHandleButton(void);
