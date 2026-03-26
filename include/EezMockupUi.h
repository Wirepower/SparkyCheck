#pragma once

#include <stdint.h>

#include "Screens.h"
#include "SparkyDisplay.h"

/**
 * Draw and handle the main UI from EEZ-exported layout data.
 *
 * Behaviour matches Screens.cpp (touch is delegated to Screens_handleTouch).
 * Text sizing aims for readability on 480px+; complex flows use Screens.cpp directly.
 */
void EezMockupUi_draw(SparkyTft* tft, ScreenId id);
ScreenId EezMockupUi_handleTouch(SparkyTft* tft, ScreenId current, uint16_t x, uint16_t y);
bool EezMockupUi_didHandleButton(void);
