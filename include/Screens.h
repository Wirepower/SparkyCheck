/**
 * SparkyCheck – UI screens and navigation.
 */

#pragma once

#include <TFT_eSPI.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Screen IDs. */
typedef enum {
  SCREEN_MAIN_MENU = 0,
  SCREEN_TEST_SELECT,
  SCREEN_TEST_FLOW,
  SCREEN_REPORT_SAVED,
  SCREEN_REPORT_LIST,
  SCREEN_SETTINGS,
  SCREEN_ROTATION,
  SCREEN_WIFI_LIST,
  SCREEN_WIFI_PASSWORD,
  SCREEN_ABOUT,
  SCREEN_UPDATES,
  SCREEN_EMAIL_SETTINGS,
  SCREEN_EMAIL_FIELD_EDIT,
  SCREEN_CHANGE_PIN,
  SCREEN_PIN_ENTER,
  SCREEN_MODE_SELECT,
  SCREEN_COUNT
} ScreenId;

/** Draw the given screen. */
void Screens_draw(TFT_eSPI* tft, ScreenId id);

/** Handle touch; returns next screen or same if no action. */
ScreenId Screens_handleTouch(TFT_eSPI* tft, ScreenId current, uint16_t x, uint16_t y);

/** Set data for report-saved screen (basename). */
void Screens_setReportSavedBasename(const char* basename);

/** Set mode-select choice (so we can highlight). */
void Screens_setModeSelectChoice(int training_or_field);

/** Set where to go after successful PIN (mode change vs email settings). */
void Screens_setPinSuccessTarget(ScreenId id);

/** True if the last handleTouch call handled a button (for button-click feedback). */
bool Screens_didHandleButton(void);

#ifdef __cplusplus
}
#endif
