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

/**
 * Map AppState rotation (0=portrait, 1=landscape) to Adafruit_GFX setRotation() for the RGB panel.
 * Native framebuffer is 800x480 (landscape); GFX r=0 is wide, r=1 is tall — opposite of app labels
 * unless remapped here. SPI TFT builds use app rotation as-is.
 */
inline int sparkyGfxRotationFromApp(int appRotation) {
#if defined(SPARKYCHECK_PANEL_43B)
  return 1 - appRotation;
#else
  return appRotation;
#endif
}

#else

#include <TFT_eSPI.h>
using SparkyTft = TFT_eSPI;

inline void sparkyDisplayFlush(SparkyTft*) {}

inline int sparkyGfxRotationFromApp(int appRotation) {
  return appRotation;
}

#endif
