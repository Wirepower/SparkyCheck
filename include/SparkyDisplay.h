/**
 * Display type for SparkyCheck: SPI TFT (TFT_eSPI) or Waveshare 4.3B RGB panel.
 */

#pragma once

#if defined(SPARKYCHECK_PANEL_43B)

#include "SparkyPanelDisplay.h"
using SparkyTft = SparkyPanelDisplay;

inline void sparkyDisplayFlush(SparkyTft* tft) {
  if (tft) tft->flush();
}

#else

#include <TFT_eSPI.h>
using SparkyTft = TFT_eSPI;

inline void sparkyDisplayFlush(SparkyTft*) {}

#endif
