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
#include "SparkyRtc.h"
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
#include "EezMockupUi.h"
#endif

SparkyTft tft;
static ScreenId s_currentScreen = SCREEN_MAIN_MENU;
static bool s_appReady = false;
static bool s_otaAutoDone = false;
static unsigned long s_otaAutoAfterMs = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("SparkyCheck – Booting");

  Buzzer_init();
  AppState_load();
#if defined(SPARKYCHECK_PANEL_43B)
  SparkyRtc_earlyInitSharedI2c();
#endif
  tft.init();
  OtaUpdate_setInstallDisplay(&tft);
  SdConfig_initAndApply();
  SparkyRtc_init();
  /* NVS wall_utc first: CPU time resets every boot/OTA; restore last saved instant before optional RTC. */
  AppState_applySavedWallClockIfInvalid();
  if (time(nullptr) < (time_t)100000 && SparkyRtc_isPresent()) SparkyRtc_syncSystemFromRtc();
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
  /* Defer OTA HTTPS so web server + AsyncTCP get heap first */
  s_otaAutoAfterMs = millis() + 8000UL;
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

  ReportGenerator_pollDeferredEmail();
  GoogleSync_tick();
  AdminPortal_tick();
  Screens_refreshLiveStatus(&tft, s_currentScreen);
  if (!s_otaAutoDone && (long)(millis() - s_otaAutoAfterMs) >= 0) {
    s_otaAutoDone = true;
    /* OTA uses HTTPUpdate + TLS; running on loopTask blew the stack canary when combined with UI saves. */
    if (xTaskCreate(OtaUpdate_autoFlowTask, "ota_auto", 16384, nullptr, 1, nullptr) != pdPASS)
      OtaUpdate_runAutoFlow();
  }

  static bool s_touchWasDown = false;
  static uint16_t s_lastTouchX = 0, s_lastTouchY = 0;
  uint16_t x = 0, y = 0;
  bool touchDown = tft.getTouch(&x, &y);

  if (touchDown) {
    if (!s_touchWasDown) {
      ScreenId next = s_currentScreen;
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
      next = EezMockupUi_handleTouch(&tft, s_currentScreen, x, y);
      if (EezMockupUi_didHandleButton() && AppState_getBuzzerEnabled()) Buzzer_beepClick();
#else
      next = Screens_handleTouch(&tft, s_currentScreen, x, y);
      if (Screens_didHandleButton() && AppState_getBuzzerEnabled()) Buzzer_beepClick();
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
      delay(90);
    } else {
      ScreenId nd = s_currentScreen;
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
      nd = EezMockupUi_handleTouchDrag(&tft, s_currentScreen, x, y);
#else
      nd = Screens_handleTouchDrag(&tft, s_currentScreen, x, y);
#endif
      if (nd != s_currentScreen) {
        s_currentScreen = nd;
        tft.setRotation(sparkyGfxRotationFromApp(AppState_getRotation()));
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
        EezMockupUi_draw(&tft, s_currentScreen);
#else
        Screens_draw(&tft, s_currentScreen);
#endif
        sparkyDisplayFlush(&tft);
      }
    }
    s_lastTouchX = x;
    s_lastTouchY = y;
  } else if (s_touchWasDown) {
    ScreenId nu = s_currentScreen;
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
    nu = EezMockupUi_handleTouchEnd(&tft, s_currentScreen, s_lastTouchX, s_lastTouchY);
#else
    nu = Screens_handleTouchEnd(&tft, s_currentScreen, s_lastTouchX, s_lastTouchY);
#endif
    if (nu != s_currentScreen) {
      s_currentScreen = nu;
      tft.setRotation(sparkyGfxRotationFromApp(AppState_getRotation()));
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
      EezMockupUi_draw(&tft, s_currentScreen);
#else
      Screens_draw(&tft, s_currentScreen);
#endif
      sparkyDisplayFlush(&tft);
    }
  }

  s_touchWasDown = touchDown;
  delay(50);
}
