#include <Arduino.h>
#include <TFT_eSPI.h>

#include "BootScreen.h"

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("SparkyCheck – Booting");

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // First boot screen: creator (Frank) + industry graphic
  BootScreen::showFirst(tft);

  // Wait for touch or timeout (e.g. 6 seconds)
  const unsigned long timeout = 6000;
  unsigned long start = millis();
  uint16_t x = 0, y = 0;
  while (millis() - start < timeout) {
    if (tft.getTouch(&x, &y)) {
      Serial.println("Boot screen skipped (touch)");
      break;
    }
    delay(50);
  }

  // TODO: second boot screen (disclaimer), then main app
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 20);
  tft.println("SparkyCheck ready.");
  tft.setCursor(20, 50);
  tft.println("(Disclaimer screen next)");
  Serial.println("Boot complete.");
}

void loop() {
  uint16_t x = 0, y = 0;
  if (tft.getTouch(&x, &y)) {
    Serial.printf("Touch: x=%d y=%d\n", x, y);
    delay(100);
  }
  delay(50);
}
