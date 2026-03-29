/**
 * Waveshare ESP32-S3-Touch-LCD-4.3B (RGB + CH422G + GT911) via ESP32_Display_Panel.
 * Draws into the panel framebuffer using Adafruit_GFX (same drawing API subset as TFT_eSPI).
 */

#pragma once

#if defined(SPARKYCHECK_PANEL_43B)

#include <Adafruit_GFX.h>
#include <stdint.h>

#ifndef TFT_BLACK
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#endif

namespace esp_panel::drivers {
class LCD;
class Touch;
}  // namespace esp_panel::drivers

namespace esp_expander {
class Base;
}

class SparkyPanelDisplay : public Adafruit_GFX {
 public:
  SparkyPanelDisplay();

  /** Initialize CH422G, LCD, touch; attach to RGB framebuffer. Blocks forever on failure. */
  void init();

  void flush();

  /** Same contract as TFT_eSPI::getTouch — coordinates in logical space for current rotation. */
  bool getTouch(uint16_t* x, uint16_t* y);

  void drawPixel(int16_t x, int16_t y, uint16_t color) override;

  /** CH422G low-level driver; null before init(). Used for TF card CS (EXIO4). */
  static esp_expander::Base* ioExpanderBase();

 private:
  bool setupBoard();

  esp_panel::drivers::LCD* lcd_ = nullptr;
  esp_panel::drivers::Touch* touch_ = nullptr;
  uint16_t* fb_ = nullptr;
  int fbW_ = 800;
  int fbH_ = 480;
};

#endif  // SPARKYCHECK_PANEL_43B
