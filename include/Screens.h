/**
 * SparkyCheck – UI screens and navigation.
 */

#pragma once

#include "SparkyDisplay.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Screen IDs. */
typedef enum {
  SCREEN_MAIN_MENU = 0,
  SCREEN_TEST_SELECT,
  SCREEN_STUDENT_ID,
  SCREEN_TEST_FLOW,
  SCREEN_REPORT_SAVED,
  SCREEN_REPORT_LIST,
  SCREEN_REPORT_VIEW,
  SCREEN_SETTINGS,
  SCREEN_ROTATION,
  SCREEN_WIFI_LIST,
  SCREEN_WIFI_PASSWORD,
  SCREEN_ABOUT,
  SCREEN_UPDATES,
  SCREEN_TRAINING_SYNC,
  SCREEN_TRAINING_SYNC_EDIT,
  SCREEN_EMAIL_SETTINGS,
  SCREEN_EMAIL_FIELD_EDIT,
  SCREEN_CHANGE_PIN,
  SCREEN_PIN_ENTER,
  SCREEN_MODE_SELECT,
  SCREEN_DATE_TIME,
  SCREEN_CLOCK_SET,
  SCREEN_COUNT
} ScreenId;

/** Draw the given screen (full clear + redraw). */
void Screens_draw(SparkyTft* tft, ScreenId id);

/** Handle touch; returns next screen or same if no action. */
ScreenId Screens_handleTouch(SparkyTft* tft, ScreenId current, uint16_t x, uint16_t y);

/** Optional drag hook (test list no longer scrolls; returns current). */
ScreenId Screens_handleTouchDrag(SparkyTft* tft, ScreenId current, uint16_t x, uint16_t y);

/** Optional touch-up hook (test select uses press-only hits; returns current). */
ScreenId Screens_handleTouchEnd(SparkyTft* tft, ScreenId current, uint16_t x, uint16_t y);

/** Set data for report-saved screen (basename). */
void Screens_setReportSavedBasename(const char* basename);

/** Set mode-select choice (so we can highlight). */
void Screens_setModeSelectChoice(int training_or_field);

/** Set where to go after successful PIN (mode change vs email settings). */
void Screens_setPinSuccessTarget(ScreenId id);
/** Set where PIN screen goes when user taps Back/cancel. */
void Screens_setPinCancelTarget(ScreenId id);
/** Clear PIN entry buffer and failed-attempt counter. */
void Screens_resetPinEntry(void);

/** True if the last handleTouch call handled a button (for button-click feedback). */
bool Screens_didHandleButton(void);

/** Brief "Setting saved" toast (used by EEZ mockup path for actions that mirror Screens_handleTouch). */
void Screens_showSavedPrompt(SparkyTft* tft, const char* detail);

#ifdef __cplusplus
}

/** Same as two-arg draw when fullClear is true. When false, repaints only the edited value line on keypad/OSK screens to reduce flicker. */
void Screens_draw(SparkyTft* tft, ScreenId id, bool fullClear);

/** Paginated test list (5 per page) + PREV / page / NEXT; EEZ shell draws title/scope then calls this. */
void Screens_drawTestSelectPagedContent(SparkyTft* tft);

/** Settings rows (5 per page) + pager + Back; EEZ shell draws title then calls this. */
void Screens_drawSettingsPagedContent(SparkyTft* tft);

/** Battery + Wi‑Fi + clock (top strip); used by full redraw and EEZ shell. */
void Screens_drawStatusBar(SparkyTft* tft);

/** Update status bar on a timer: clock when the string changes (~1s check), battery/Wi‑Fi periodically
 *  on screens where the top-left icons do not cover other widgets. */
void Screens_refreshLiveStatus(SparkyTft* tft, ScreenId currentScreen);
#endif
