#include <Arduino.h>

#include "SparkyDisplay.h"
#include "BootScreen.h"
#include "AppState.h"
#include "Screens.h"
#include "ReportGenerator.h"
#include "Buzzer.h"
#include "WifiManager.h"
#include "OtaUpdate.h"
#include "GoogleSync.h"
#include "SdConfig.h"
#include "AdminPortal.h"

#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
#include "EezMockupUi.h"
#endif

SparkyTft tft;
static ScreenId s_currentScreen = SCREEN_MAIN_MENU;
static bool s_appReady = false;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("SparkyCheck – Booting");

  Buzzer_init();
  AppState_load();
  SdConfig_initAndApply();
  tft.init();
  tft.setRotation(sparkyGfxRotationFromApp(AppState_getRotation()));
  tft.fillScreen(TFT_BLACK);

  /* Boot 1: Creator + graphic */
  BootScreen::showFirst(tft);
  unsigned long start = millis();
  uint16_t x = 0, y = 0;
  bool adminGesture = false;
  unsigned long holdStart = 0;
  while (millis() - start < 6000) {
    if (tft.getTouch(&x, &y)) {
      bool onLogo = BootScreen::isBootLogoTouchRegion(tft, (int)x, (int)y);
      if (onLogo) {
        if (holdStart == 0) holdStart = millis();
        if (millis() - holdStart >= 2500) { adminGesture = true; break; }
      } else {
        break;  // normal tap anywhere else
      }
    } else {
      if (holdStart != 0) break;  // released before hold threshold
    }
    delay(30);
  }

  /* Boot 2: Disclaimer (must accept) */
  BootScreen::showDisclaimer(tft);
  Buzzer_startupChime();

  ReportGenerator_init();
  WifiManager_reconnectSaved();
  OtaUpdate_init();
  GoogleSync_init();
  AdminPortal_init();
  OtaUpdate_runAutoFlow();
  if (adminGesture) {
    Screens_setModeSelectChoice(AppState_getMode() == APP_MODE_FIELD ? 1 : 0);
    Screens_setPinSuccessTarget(SCREEN_MODE_SELECT);
    Screens_setPinCancelTarget(SCREEN_MAIN_MENU);
    Screens_resetPinEntry();
    s_currentScreen = SCREEN_PIN_ENTER;
  } else {
    s_currentScreen = SCREEN_MAIN_MENU;
  }
  tft.setRotation(sparkyGfxRotationFromApp(AppState_getRotation()));
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
  EezMockupUi_draw(&tft, s_currentScreen);
#else
  Screens_draw(&tft, s_currentScreen);
#endif
  sparkyDisplayFlush(&tft);

  s_appReady = true;
  Serial.println("SparkyCheck ready.");
}

void loop() {
  if (!s_appReady) return;

  GoogleSync_tick();
  AdminPortal_tick();

  static bool s_touchWasDown = false;
  uint16_t x = 0, y = 0;
  bool touchDown = tft.getTouch(&x, &y);
  if (touchDown && !s_touchWasDown) {
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
    ScreenId next = EezMockupUi_handleTouch(&tft, s_currentScreen, x, y);
    if (EezMockupUi_didHandleButton() && AppState_getBuzzerEnabled())
      Buzzer_beepClick();
#else
    ScreenId next = Screens_handleTouch(&tft, s_currentScreen, x, y);
    if (Screens_didHandleButton() && AppState_getBuzzerEnabled())
      Buzzer_beepClick();
#endif
    if (next != s_currentScreen) {
      s_currentScreen = next;
      tft.setRotation(sparkyGfxRotationFromApp(AppState_getRotation()));
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
      EezMockupUi_draw(&tft, s_currentScreen);
#else
      Screens_draw(&tft, s_currentScreen);
#endif
      sparkyDisplayFlush(&tft);
    }
    // Small debounce for tap transitions; held touches won't retrigger.
    delay(90);
  }
  s_touchWasDown = touchDown;
  delay(50);
}
