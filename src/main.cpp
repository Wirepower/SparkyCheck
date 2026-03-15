#include <Arduino.h>
#include <TFT_eSPI.h>

#include "BootScreen.h"
#include "AppState.h"
#include "Screens.h"
#include "ReportGenerator.h"
#include "Buzzer.h"
#include "WifiManager.h"
#include "OtaUpdate.h"

TFT_eSPI tft = TFT_eSPI();
static ScreenId s_currentScreen = SCREEN_MAIN_MENU;
static bool s_appReady = false;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("SparkyCheck – Booting");

  Buzzer_init();
  AppState_load();
  tft.init();
  tft.setRotation(AppState_getRotation());
  tft.fillScreen(TFT_BLACK);

  /* Boot 1: Creator + graphic */
  BootScreen::showFirst(tft);
  unsigned long start = millis();
  uint16_t x = 0, y = 0;
  while (millis() - start < 6000) {
    if (tft.getTouch(&x, &y)) break;
    delay(50);
  }

  /* Boot 2: Disclaimer (must accept) */
  BootScreen::showDisclaimer(tft);
  Buzzer_startupChime();

  ReportGenerator_init();
  WifiManager_reconnectSaved();
  OtaUpdate_init();
  OtaUpdate_runAutoFlow();
  Screens_setModeSelectChoice(AppState_getMode() == APP_MODE_FIELD ? 1 : 0);
  s_currentScreen = SCREEN_MAIN_MENU;
  tft.setRotation(AppState_getRotation());
  Screens_draw(&tft, s_currentScreen);

  s_appReady = true;
  Serial.println("SparkyCheck ready.");
}

void loop() {
  if (!s_appReady) return;

  uint16_t x = 0, y = 0;
  if (tft.getTouch(&x, &y)) {
    ScreenId next = Screens_handleTouch(&tft, s_currentScreen, x, y);
    if (Screens_didHandleButton() && AppState_getBuzzerEnabled())
      Buzzer_beepClick();
    if (next != s_currentScreen) {
      s_currentScreen = next;
      tft.setRotation(AppState_getRotation());
      Screens_draw(&tft, s_currentScreen);
    }
    delay(120);
  }
  delay(50);
}
