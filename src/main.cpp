#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("SparkyCheck TFT + GT911 I2C Test");

  tft.begin();
  tft.setRotation(1);  // Try 0, 1, 2, 3 if image is wrong
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 20);
  tft.println("SparkyCheck");
  tft.setCursor(20, 60);
  tft.println("Touch Test");
  Serial.println("TFT ready");
}

void loop() {
  uint16_t x = 0, y = 0;
  if (tft.getTouch(&x, &y)) {
    Serial.printf("Touch: x=%d y=%d\n", x, y);
    tft.fillCircle(x, y, 8, TFT_RED);
    delay(100);
  }
  delay(50);
}