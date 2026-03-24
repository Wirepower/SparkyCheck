#include "Screens.h"
#include "AppState.h"
#include "ReportGenerator.h"
#include "TestLimits.h"
#include "Buzzer.h"
#include "WifiManager.h"
#include "OtaUpdate.h"
#include "GoogleSync.h"
#include "Standards.h"
#include "VerificationSteps.h"
#include "SparkyDisplay.h"
#include <WiFi.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const uint16_t kBg = 0x18E3;
static const uint16_t kBtn = 0x2D6A;
static const uint16_t kAccent = 0xFD20;
static const uint16_t kWhite = 0xFFFF;
static const uint16_t kGreen = 0x07E0;
static const uint16_t kRed = 0xF800;

static char s_reportSavedBasename[48] = "";
static int s_modeSelectChoice = 0;  /* 0 = Training, 1 = Field */

/* Verification coach state */
static int s_selectedTestType = 0;
static int s_stepIndex = 0;
static float s_resultValue = 0.0f;
static bool s_resultIsSheathedHeating = false;
static int s_flowPhase = 0;   /* 0 = steps, 1 = result screen (pass/fail) */
static bool s_resultPass = false;
static const char* s_resultLabel = NULL;
static const char* s_resultUnit = NULL;
static const char* s_resultClause = NULL;
static char s_studentId[APP_STATE_DEVICE_ID_LEN] = "";
static char s_studentInput[APP_STATE_DEVICE_ID_LEN] = "";
static int s_studentInputLen = 0;
static unsigned long s_testStartedMs = 0;
static unsigned long s_testCompletedMs = 0;
static float s_rcdRequiredMaxMs = 0.0f;
static VerifyResultKind s_resultInputKind = RESULT_NONE;
static char s_resultInput[16] = "";
static int s_resultInputLen = 0;
static int s_resultInputUnitIdx = 0;

typedef struct {
  const char* label;
  float toCanonical;
} ResultUnitOption;

static const ResultUnitOption kResultUnitsOhm[] = {
  { "ohm", 1.0f },
  { "kOhm", 1000.0f },
  { "MOhm", 1000000.0f },
};

static const ResultUnitOption kResultUnitsInsulation[] = {
  { "ohm", 0.000001f },
  { "kOhm", 0.001f },
  { "MOhm", 1.0f },
  { "GOhm", 1000.0f },
};

static const ResultUnitOption kResultUnitsRcd[] = {
  { "ms", 1.0f },
  { "s", 1000.0f },
};

/* PIN entry: where to go after correct PIN */
static ScreenId s_pinSuccessTarget = SCREEN_MODE_SELECT;
static ScreenId s_pinCancelTarget = SCREEN_SETTINGS;

static const int kPinMinLen = 4;
static const int kPinMaxLen = 8;
static const int kPinMaxAttempts = 3;

/* PIN entry state (variable length, min 4 digits) */
static char s_pinDigits[kPinMaxLen + 1] = "";
static int s_pinLen = 0;
static int s_pinFailAttempts = 0;

/* Change PIN: step 0 = enter new, step 1 = confirm; buffers for comparison */
static int s_changePinStep = 0;
static char s_newPinBuf[kPinMaxLen + 1] = "";
static int s_newPinLen = 0;

/* Email settings edit: which field (0=server,1=port,2=user,3=pass,4=reportTo) and buffer */
static int s_editingEmailField = 0;
static char s_editBuffer[APP_STATE_EMAIL_STR_LEN] = "";
static int s_editLen = 0;

/* Training sync edit state: field 0=url, 1=token, 2=cubicle, 3=device id */
static int s_syncEditField = 0;
static char s_syncEditBuffer[APP_STATE_TRAINING_SYNC_URL_LEN] = "";
static int s_syncEditLen = 0;
static bool s_syncEditUpper = false;

/* WiFi list / password state */
static WifiNetwork s_networks[WIFI_MAX_SSIDS];
static int s_networkCount = 0;
static char s_selectedSsid[WIFI_SSID_LEN] = "";
static char s_wifiPass[WIFI_PASS_LEN] = "";
static int s_wifiPassLen = 0;
static bool s_trainingSettingsUnlocked = false;

static int getW(SparkyTft* tft) { return tft->width(); }
static int getH(SparkyTft* tft) { return tft->height(); }

static bool s_handledButton = false;
static ScreenId handled(ScreenId id) { s_handledButton = true; return id; }

bool Screens_didHandleButton(void) {
  return s_handledButton;
}

static bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void showPrompt(SparkyTft* tft, const char* line1, const char* line2, uint16_t frameColor) {
  if (!tft || !line1 || !line1[0]) return;
  int w = getW(tft);
  int h = getH(tft);
  bool hasLine2 = (line2 && line2[0]);
  int boxW = w - 24;
  int boxH = hasLine2 ? 42 : 30;
  int boxX = 12;
  int boxY = h - boxH - 8;
  tft->fillRoundRect(boxX, boxY, boxW, boxH, 6, kBg);
  tft->drawRoundRect(boxX, boxY, boxW, boxH, 6, frameColor);
  tft->setTextSize(1);
  tft->setTextColor(kWhite, kBg);
  int tw1 = (int)strlen(line1) * 6;
  int tx1 = boxX + (boxW - tw1) / 2;
  if (tx1 < boxX + 4) tx1 = boxX + 4;
  tft->setCursor(tx1, boxY + (hasLine2 ? 9 : 11));
  tft->print(line1);
  if (hasLine2) {
    int tw2 = (int)strlen(line2) * 6;
    int tx2 = boxX + (boxW - tw2) / 2;
    if (tx2 < boxX + 4) tx2 = boxX + 4;
    tft->setCursor(tx2, boxY + 23);
    tft->print(line2);
  }
  sparkyDisplayFlush(tft);
  delay(900);
}

static void showSavedPrompt(SparkyTft* tft, const char* detail) {
  showPrompt(tft, "Setting saved", detail, kGreen);
}

static void getResultUnitOptions(VerifyResultKind kind, const ResultUnitOption** out, int* outCount, int* outDefaultIdx) {
  if (!out || !outCount || !outDefaultIdx) return;
  switch (kind) {
    case RESULT_CONTINUITY_OHM:
    case RESULT_EFLI_OHM:
      *out = kResultUnitsOhm;
      *outCount = sizeof(kResultUnitsOhm) / sizeof(kResultUnitsOhm[0]);
      *outDefaultIdx = 0;
      return;
    case RESULT_IR_MOHM:
    case RESULT_IR_MOHM_SHEATHED:
      *out = kResultUnitsInsulation;
      *outCount = sizeof(kResultUnitsInsulation) / sizeof(kResultUnitsInsulation[0]);
      *outDefaultIdx = 2;
      return;
    case RESULT_RCD_MS:
    case RESULT_RCD_REQUIRED_MAX_MS:
      *out = kResultUnitsRcd;
      *outCount = sizeof(kResultUnitsRcd) / sizeof(kResultUnitsRcd[0]);
      *outDefaultIdx = 0;
      return;
    default:
      *out = kResultUnitsOhm;
      *outCount = 1;
      *outDefaultIdx = 0;
      return;
  }
}

static int findResultUnitIndex(const ResultUnitOption* units, int count, const char* label) {
  if (!units || count <= 0 || !label || !label[0]) return -1;
  for (int i = 0; i < count; i++) {
    if (strcmp(units[i].label, label) == 0) return i;
  }
  return -1;
}

static void trimNumberString(char* text) {
  if (!text) return;
  int len = (int)strlen(text);
  while (len > 0 && text[len - 1] == '0') text[--len] = '\0';
  if (len > 0 && text[len - 1] == '.') text[--len] = '\0';
  if (len <= 0) strcpy(text, "0");
}

static void resetResultEntryInput(void) {
  s_resultInputKind = RESULT_NONE;
  s_resultInput[0] = '\0';
  s_resultInputLen = 0;
  s_resultInputUnitIdx = 0;
  s_rcdRequiredMaxMs = 0.0f;
}

static void ensureResultEntryInputState(VerifyResultKind kind) {
  const ResultUnitOption* units = nullptr;
  int unitCount = 0;
  int defaultIdx = 0;
  getResultUnitOptions(kind, &units, &unitCount, &defaultIdx);
  if (s_resultInputKind != kind) {
    s_resultInputKind = kind;
    s_resultInput[0] = '\0';
    s_resultInputLen = 0;
    s_resultInputUnitIdx = defaultIdx;
    if (kind == RESULT_RCD_REQUIRED_MAX_MS && s_rcdRequiredMaxMs > 0.0f) {
      snprintf(s_resultInput, sizeof(s_resultInput), "%.6f", (double)s_rcdRequiredMaxMs);
      trimNumberString(s_resultInput);
      s_resultInputLen = (int)strlen(s_resultInput);
      return;
    }
    if (s_resultUnit && s_resultUnit[0]) {
      int idx = findResultUnitIndex(units, unitCount, s_resultUnit);
      if (idx >= 0) s_resultInputUnitIdx = idx;
    }
    if (s_resultUnit && s_resultUnit[0]) {
      snprintf(s_resultInput, sizeof(s_resultInput), "%.6f", (double)s_resultValue);
      trimNumberString(s_resultInput);
      s_resultInputLen = (int)strlen(s_resultInput);
    }
    return;
  }
  if (s_resultInputUnitIdx < 0 || s_resultInputUnitIdx >= unitCount)
    s_resultInputUnitIdx = defaultIdx;
}

static int syncEditMaxLen(void) {
  if (s_syncEditField == 1) return APP_STATE_TRAINING_SYNC_TOKEN_LEN;
  if (s_syncEditField == 2) return APP_STATE_TRAINING_SYNC_CUBICLE_LEN;
  if (s_syncEditField == 3) return APP_STATE_DEVICE_ID_LEN;
  return APP_STATE_TRAINING_SYNC_URL_LEN;
}

static void loadSyncEditField(int field) {
  s_syncEditField = field;
  s_syncEditUpper = false;
  s_syncEditLen = 0;
  s_syncEditBuffer[0] = '\0';
  if (field == 0) AppState_getTrainingSyncEndpoint(s_syncEditBuffer, sizeof(s_syncEditBuffer));
  else if (field == 1) AppState_getTrainingSyncToken(s_syncEditBuffer, sizeof(s_syncEditBuffer));
  else if (field == 2) AppState_getTrainingSyncCubicleId(s_syncEditBuffer, sizeof(s_syncEditBuffer));
  else AppState_getDeviceIdOverride(s_syncEditBuffer, sizeof(s_syncEditBuffer));
  s_syncEditLen = strlen(s_syncEditBuffer);
  if (s_syncEditLen >= syncEditMaxLen()) s_syncEditLen = syncEditMaxLen() - 1;
  s_syncEditBuffer[s_syncEditLen] = '\0';
}

static char applyLetterCase(char c, bool upper) {
  if (!upper) return c;
  if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
  return c;
}

static bool normalizeStudentId(const char* raw, char* out, unsigned out_size) {
  if (!raw || !out || out_size < 3) return false;
  char digits[APP_STATE_DEVICE_ID_LEN] = "";
  int d = 0;
  bool sawAny = false;
  for (int i = 0; raw[i] && d < (int)sizeof(digits) - 1; i++) {
    char c = raw[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (!sawAny && c == 'S') { sawAny = true; continue; }
    sawAny = true;
    if (c >= '0' && c <= '9') digits[d++] = c;
  }
  digits[d] = '\0';
  if (d <= 0) return false;
  snprintf(out, out_size, "S%s", digits);
  return true;
}

static const char* syncTestKeyForId(int testId) {
  switch (testId) {
    case VERIFY_CONTINUITY: return "earth_continuity_conductors";
    case VERIFY_INSULATION: return "insulation_resistance";
    case VERIFY_POLARITY: return "polarity";
    case VERIFY_EARTH_CONTINUITY: return "earth_continuity_cpc";
    case VERIFY_CIRCUIT_CONNECTIONS: return "correct_circuit_connections";
    case VERIFY_EFLI: return "earth_fault_loop_impedance";
    case VERIFY_RCD: return "rcd_operation";
    case VERIFY_SWP_DISCONNECT_RECONNECT_MOTOR: return "swp_disconnect_reconnect_motor";
    case VERIFY_SWP_DISCONNECT_RECONNECT_APPLIANCE: return "swp_disconnect_reconnect_appliance";
    case VERIFY_SWP_DISCONNECT_RECONNECT_HEATER_SHEATHED: return "swp_disconnect_reconnect_heater_sheathed";
    default: return "";
  }
}

static const char* syncTargetLabel(TrainingSyncTarget t) {
  switch (t) {
    case TRAINING_SYNC_TARGET_GOOGLE: return "Google Sheets";
    case TRAINING_SYNC_TARGET_SHAREPOINT: return "SharePoint";
    default: return "Auto";
  }
}

static void syncTrainingFlowEvent(const char* event, bool include_result, const char* report_id_or_null) {
  if (AppState_getMode() != APP_MODE_TRAINING) return;
  if (s_selectedTestType < 0 || s_selectedTestType >= VERIFY_TEST_COUNT) return;

  VerifyTestId tid = (VerifyTestId)s_selectedTestType;
  int count = VerificationSteps_getStepCount(tid);
  if (count <= 0) return;

  VerifyStep step;
  const char* stepTitle = "";
  int stepIndex = 0;
  if (s_flowPhase == 1) {
    stepTitle = "Result";
    stepIndex = count;
  } else {
    int idx = s_stepIndex;
    if (idx < 0) idx = 0;
    if (idx >= count) idx = count - 1;
    VerificationSteps_getStep(tid, idx, &step);
    stepTitle = step.title ? step.title : "";
    stepIndex = idx + 1;
  }

  char valueBuf[24] = "";
  if (include_result) {
    if (s_resultUnit && s_resultUnit[0]) snprintf(valueBuf, sizeof(valueBuf), "%.3f", (double)s_resultValue);
    else snprintf(valueBuf, sizeof(valueBuf), "Verified");
  }

  GoogleSyncResult gs;
  gs.sync_event = event ? event : "update";
  gs.test_id = s_selectedTestType;
  gs.test_key = syncTestKeyForId(s_selectedTestType);
  gs.step_title = stepTitle;
  gs.step_index = stepIndex;
  gs.step_count = count;
  gs.has_result = include_result;
  gs.session_id = "";
  gs.student_id = s_studentId;
  gs.test_started_ms = s_testStartedMs;
  gs.test_completed_ms = s_testCompletedMs;
  gs.report_id = report_id_or_null ? report_id_or_null : "";
  gs.test_name = VerificationSteps_getTestName(tid);
  gs.value = include_result ? valueBuf : "";
  gs.unit = include_result ? (s_resultUnit ? s_resultUnit : "") : "";
  gs.passed = s_resultPass;
  gs.clause = include_result ? (s_resultClause ? s_resultClause : "") : "";
  GoogleSync_sendResult(&gs);
}

void Screens_setReportSavedBasename(const char* basename) {
  strncpy(s_reportSavedBasename, basename ? basename : "", sizeof(s_reportSavedBasename) - 1);
  s_reportSavedBasename[sizeof(s_reportSavedBasename) - 1] = '\0';
}

void Screens_setModeSelectChoice(int training_or_field) {
  s_modeSelectChoice = training_or_field;
}

void Screens_setPinSuccessTarget(ScreenId id) {
  s_pinSuccessTarget = id;
}

void Screens_setPinCancelTarget(ScreenId id) {
  s_pinCancelTarget = id;
}

void Screens_resetPinEntry(void) {
  s_pinLen = 0;
  s_pinDigits[0] = '\0';
  s_pinFailAttempts = 0;
}

void Screens_draw(SparkyTft* tft, ScreenId id) {
  int w = getW(tft);
  int h = getH(tft);
  tft->fillScreen(kBg);

  switch (id) {
    case SCREEN_MODE_SELECT: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 20);
      tft->print("Select mode");
      int btnW = w - 40, btnH = 56, y = 80;
      tft->fillRoundRect(20, y, btnW, btnH, 8, s_modeSelectChoice == 0 ? kGreen : kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(TFT_BLACK, s_modeSelectChoice == 0 ? kGreen : kBtn);
      tft->setCursor(40, y + 18);
      tft->print("Training (apprentice / supervised)");
      y += 70;
      tft->fillRoundRect(20, y, btnW, btnH, 8, s_modeSelectChoice == 1 ? kGreen : kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(TFT_BLACK, s_modeSelectChoice == 1 ? kGreen : kBtn);
      tft->setCursor(40, y + 18);
      tft->print("Field (qualified electrician)");
      y += 70;
      tft->fillRoundRect(w/2 - 60, y, 120, 44, 8, kAccent);
      tft->drawRoundRect(w/2 - 60, y, 120, 44, 8, kWhite);
      tft->setTextColor(TFT_BLACK, kAccent);
      tft->setCursor(w/2 - 35, y + 12);
      tft->print("Continue");
      break;
    }
    case SCREEN_MAIN_MENU: {
      bool panelWide = (w >= 700);
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 16);
      tft->print("SparkyCheck");
      tft->setTextSize(1);
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 44);
      tft->print(AppState_isFieldMode() ? "Field mode" : "Training mode");
      int btnW = w - 40, btnH = panelWide ? 64 : 44, y = panelWide ? 88 : 76;
      int labelSize = panelWide ? 2 : 1;
      int labelYOff = panelWide ? 24 : 12;
      tft->fillRoundRect(20, y, btnW, btnH, 8, kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setTextSize(labelSize);
      tft->setCursor(panelWide ? 44 : 36, y + labelYOff);
      tft->print("Start verification");
      y += btnH + (panelWide ? 14 : 12);
      tft->fillRoundRect(20, y, btnW, btnH, 8, kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(panelWide ? 44 : 36, y + labelYOff);
      tft->print("View reports");
      y += btnH + (panelWide ? 14 : 12);
      tft->fillRoundRect(20, y, btnW, btnH, 8, kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(panelWide ? 44 : 36, y + labelYOff);
      tft->print("Settings");
      break;
    }
    case SCREEN_TEST_SELECT: {
      bool panelWide = (w >= 700);
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(panelWide ? 3 : 2);
      tft->setCursor(20, 10);
      tft->print("Select test");
      int backW = panelWide ? 90 : 48;
      int backH = panelWide ? 34 : 24;
      int backX = w - (panelWide ? 102 : 62);
      int backY = panelWide ? 10 : 8;
      tft->fillRoundRect(backX, backY, backW, backH, 6, kBtn);
      tft->drawRoundRect(backX, backY, backW, backH, 6, kWhite);
      tft->setTextSize(1);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(backX + (panelWide ? 20 : 6), backY + (panelWide ? 11 : 4));
      tft->print("Back");
      tft->setTextSize(panelWide ? 2 : 1);
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, panelWide ? 50 : 38);
      { char scope[96];
        Standards_getVerificationScopeLine(scope, sizeof(scope));
        tft->print(scope);
      }
      int rowH = panelWide ? 38 : 18, y = panelWide ? 84 : 48;
      tft->setTextWrap(false);
      for (int i = 0; i < VERIFY_TEST_COUNT; i++) {
        tft->fillRoundRect(20, y, w - 40, rowH - 2, 6, kBtn);
        tft->drawRoundRect(20, y, w - 40, rowH - 2, 6, kWhite);
        tft->setTextColor(kWhite, kBtn);
        tft->setTextSize(panelWide ? 2 : 1);
        tft->setCursor(28, y + (panelWide ? 11 : 5));
        tft->print(VerificationSteps_getTestName((VerifyTestId)i));
        y += rowH;
      }
      tft->setTextWrap(true);
      break;
    }
    case SCREEN_STUDENT_ID: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 10);
      tft->print("Student ID");
      tft->setTextSize(1);
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 36);
      tft->print("Enter digits only (S is added automatically).");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(20, 52);
      char norm[APP_STATE_DEVICE_ID_LEN] = "";
      if (normalizeStudentId(s_studentInput, norm, sizeof(norm)))
        tft->print(norm);
      else
        tft->print("S");

      int cellW = (w - 50) / 3, cellH = 34, startY = 82, gap = 6;
      const char* keys = "123456789D0E";  /* D=delete, E=start */
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
          int idx = row * 3 + col;
          int kx = 20 + col * (cellW + gap), ky = startY + row * (cellH + gap);
          uint16_t cellBg = kBtn;
          if (keys[idx] == 'D') cellBg = kAccent;
          else if (keys[idx] == 'E') cellBg = kGreen;
          tft->fillRoundRect(kx, ky, cellW, cellH, 6, cellBg);
          tft->drawRoundRect(kx, ky, cellW, cellH, 6, kWhite);
          tft->setTextColor(keys[idx] == 'E' ? TFT_BLACK : kWhite, cellBg);
          if (keys[idx] == 'D') { tft->setCursor(kx + cellW/2 - 12, ky + 11); tft->print("Del"); }
          else if (keys[idx] == 'E') { tft->setCursor(kx + cellW/2 - 14, ky + 11); tft->print("Start"); }
          else { tft->setCursor(kx + cellW/2 - 4, ky + 11); tft->print(keys[idx]); }
        }
      }
      tft->fillRoundRect(w - 62, 8, 48, 24, 6, kBtn);
      tft->drawRoundRect(w - 62, 8, 48, 24, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(w - 56, 12);
      tft->print("Back");
      break;
    }
    case SCREEN_TEST_FLOW: {
      if (s_flowPhase == 1) {
        tft->setTextColor(kWhite, kBg);
        tft->setTextSize(2);
        tft->setCursor(20, 16);
        tft->print("Result");
        tft->fillRoundRect(w - 70, 6, 50, 24, 6, kBtn);
        tft->drawRoundRect(w - 70, 6, 50, 24, 6, kWhite);
        tft->setTextColor(kWhite, kBtn);
        tft->setTextSize(1);
        tft->setCursor(w - 62, 12);
        tft->print("Back");
        tft->fillRect(20, 52, w - 40, 72, s_resultPass ? kGreen : kRed);
        tft->setTextColor(TFT_BLACK, s_resultPass ? kGreen : kRed);
        tft->setTextSize(3);
        tft->setCursor(w/2 - 30, 78);
        tft->print(s_resultPass ? "PASS" : "FAIL");
        tft->setTextSize(1);
        tft->setTextColor(kWhite, kBg);
        tft->setCursor(20, 138);
        char buf[80];
        if (s_resultUnit && s_resultUnit[0])
          snprintf(buf, sizeof(buf), "%s: %.3f %s", s_resultLabel ? s_resultLabel : "", (double)s_resultValue, s_resultUnit);
        else
          snprintf(buf, sizeof(buf), "%s: Verified", s_resultLabel ? s_resultLabel : "Test");
        tft->print(buf);
        tft->setCursor(20, 154);
        if (s_selectedTestType == (int)VERIFY_RCD && s_rcdRequiredMaxMs > 0.0f) {
          char crit[64];
          snprintf(crit, sizeof(crit), "Criterion: <= %.3f ms", (double)s_rcdRequiredMaxMs);
          tft->print(crit);
          tft->setCursor(20, 166);
        }
        tft->print(s_resultClause ? s_resultClause : "");
        int btnY = h - 52;
        tft->fillRoundRect(20, btnY, w - 40, 44, 8, kAccent);
        tft->drawRoundRect(20, btnY, w - 40, 44, 8, kWhite);
        tft->setTextColor(TFT_BLACK, kAccent);
        tft->setCursor(w/2 - 42, btnY + 12);
        tft->print("End session");
        break;
      }
      int count = VerificationSteps_getStepCount((VerifyTestId)s_selectedTestType);
      if (s_stepIndex >= count) break;
      VerifyStep step;
      VerificationSteps_getStep((VerifyTestId)s_selectedTestType, s_stepIndex, &step);
      tft->setTextColor(step.type == STEP_SAFETY ? kAccent : kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 10);
      tft->print(step.title);
      tft->fillRoundRect(w - 70, 6, 50, 24, 6, kBtn);
      tft->drawRoundRect(w - 70, 6, 50, 24, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setTextSize(1);
      tft->setCursor(w - 62, 12);
      tft->print("Back");
      tft->setTextSize(1);
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 36);
      tft->print(step.clause);
      tft->setCursor(w - 72, 36);
      char prog[16];
      snprintf(prog, sizeof(prog), "Step %d of %d", s_stepIndex + 1, count);
      tft->print(prog);
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(20, 52);
      tft->print(step.instruction);
      if (step.type == STEP_SAFETY) {
        int btnY = h - 54;
        tft->fillRoundRect(20, btnY, w - 40, 44, 8, kGreen);
        tft->drawRoundRect(20, btnY, w - 40, 44, 8, kWhite);
        tft->setTextColor(TFT_BLACK, kGreen);
        tft->setCursor(w/2 - 38, btnY + 12);
        tft->print("OK / I have done this");
      } else if (step.type == STEP_VERIFY_YESNO) {
        int btnY = h - 54, half = (w - 50) / 2;
        tft->fillRoundRect(20, btnY, half, 44, 8, kGreen);
        tft->drawRoundRect(20, btnY, half, 44, 8, kWhite);
        tft->setTextColor(TFT_BLACK, kGreen);
        tft->setCursor(20 + half/2 - 12, btnY + 12);
        tft->print("Yes");
        tft->fillRoundRect(30 + half, btnY, half, 44, 8, kBtn);
        tft->drawRoundRect(30 + half, btnY, half, 44, 8, kWhite);
        tft->setTextColor(kWhite, kBtn);
        tft->setCursor(30 + half + half/2 - 10, btnY + 12);
        tft->print("No");
      } else if (step.type == STEP_INFO) {
        int btnY = h - 54;
        tft->fillRoundRect(20, btnY, w - 40, 44, 8, kGreen);
        tft->drawRoundRect(20, btnY, w - 40, 44, 8, kWhite);
        tft->setTextColor(TFT_BLACK, kGreen);
        tft->setCursor(w/2 - 12, btnY + 12);
        tft->print("OK");
      } else if (step.type == STEP_RESULT_ENTRY) {
        ensureResultEntryInputState(step.resultKind);
        const ResultUnitOption* units = nullptr;
        int unitCount = 0;
        int defaultIdx = 0;
        getResultUnitOptions(step.resultKind, &units, &unitCount, &defaultIdx);
        if (s_resultInputUnitIdx < 0 || s_resultInputUnitIdx >= unitCount) s_resultInputUnitIdx = defaultIdx;

        tft->setTextColor(kWhite, kBg);
        if (step.resultKind == RESULT_RCD_MS && s_rcdRequiredMaxMs > 0.0f) {
          char reqBuf[44];
          snprintf(reqBuf, sizeof(reqBuf), "Required max: %.3f ms", (double)s_rcdRequiredMaxMs);
          tft->setCursor(20, 84);
          tft->print(reqBuf);
        }
        tft->setCursor(20, 98);
        tft->print("Value:");
        tft->setCursor(20, 112);
        tft->print(s_resultInputLen > 0 ? s_resultInput : "(none)");
        tft->print(" ");
        tft->print(units[s_resultInputUnitIdx].label);

        tft->fillRoundRect(w - 74, 96, 54, 20, 6, kAccent);
        tft->drawRoundRect(w - 74, 96, 54, 20, 6, kWhite);
        tft->setTextColor(TFT_BLACK, kAccent);
        tft->setCursor(w - 60, 102);
        tft->print("Del");

        int uy = 120, uh = 18, uGap = 4;
        int uw = (w - 40 - (unitCount - 1) * uGap) / unitCount;
        for (int i = 0; i < unitCount; i++) {
          int ux = 20 + i * (uw + uGap);
          uint16_t bg = (i == s_resultInputUnitIdx) ? kGreen : kBtn;
          tft->fillRoundRect(ux, uy, uw, uh, 6, bg);
          tft->drawRoundRect(ux, uy, uw, uh, 6, kWhite);
          tft->setTextColor(i == s_resultInputUnitIdx ? TFT_BLACK : kWhite, bg);
          int tx = ux + uw / 2 - ((int)strlen(units[i].label) * 6) / 2;
          tft->setCursor(tx < ux + 2 ? ux + 2 : tx, uy + 6);
          tft->print(units[i].label);
        }

        const char* keys[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "OK" };
        int cellW = (w - 50) / 3, cellH = 20, gap = 6, startX = 20, startY = 142;
        for (int row = 0; row < 4; row++) {
          for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            int kx = startX + col * (cellW + gap), ky = startY + row * (cellH + gap);
            uint16_t bg = (strcmp(keys[idx], "OK") == 0) ? kGreen : kBtn;
            tft->fillRoundRect(kx, ky, cellW, cellH, 6, bg);
            tft->drawRoundRect(kx, ky, cellW, cellH, 6, kWhite);
            tft->setTextColor(strcmp(keys[idx], "OK") == 0 ? TFT_BLACK : kWhite, bg);
            int tw = (int)strlen(keys[idx]) * 6;
            tft->setCursor(kx + (cellW - tw) / 2, ky + 6);
            tft->print(keys[idx]);
          }
        }
      }
      break;
    }
    case SCREEN_REPORT_SAVED: {
      tft->setTextColor(kGreen, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 30);
      tft->print("Report saved");
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(1);
      tft->setCursor(20, 70);
      tft->print(s_reportSavedBasename);
      tft->setCursor(20, 90);
      tft->print("(.csv and .html on storage)");
      int btnY = h - 56;
      tft->fillRoundRect(w/2 - 50, btnY, 100, 44, 8, kBtn);
      tft->drawRoundRect(w/2 - 50, btnY, 100, 44, 8, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(w/2 - 18, btnY + 12);
      tft->print("OK");
      break;
    }
    case SCREEN_REPORT_LIST: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 16);
      tft->print("Reports");
      static char listBuf[384];  /* Enough for ~20 report names; avoids large BSS */
      int n = ReportGenerator_listReports(listBuf, sizeof(listBuf));
      tft->setTextSize(1);
      tft->setCursor(20, 50);
      if (n == 0)
        tft->print("(No reports yet)");
      else {
        int y = 50;
        char* p = listBuf;
        for (int i = 0; i < n && y < h - 60; i++) {
          tft->setCursor(20, y);
          tft->print(p);
          while (*p && *p != '\n') p++;
          if (*p) p++;
          y += 18;
        }
      }
      int btnY = h - 52;
      tft->fillRoundRect(20, btnY, 100, 40, 8, kBtn);
      tft->drawRoundRect(20, btnY, 100, 40, 8, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(44, btnY + 10);
      tft->print("Back");
      break;
    }
    case SCREEN_SETTINGS: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 10);
      tft->print("Settings");
      int btnW = w - 40, btnH = 30, y = 42, gap = 2;
      bool field = AppState_isFieldMode();
      int nRows = field ? 7 : 8;
      for (int i = 0; i < nRows; i++) {
        const char* label = "";
        if (i == 0) label = "Screen rotation";
        else if (i == 1) label = "WiFi connection";
        else if (i == 2) label = "Buzzer (sound)";
        else if (i == 3) label = "About";
        else if (i == 4) label = "Firmware updates";
        else if (i == 5) label = "Email settings";
        else if (i == 6) label = "Mode change (boot hold)";
        else label = "Change PIN";
        tft->fillRoundRect(20, y, btnW, btnH, 6, kBtn);
        tft->drawRoundRect(20, y, btnW, btnH, 6, kWhite);
        tft->setTextColor(kWhite, kBtn);
        tft->setTextSize(1);
        tft->setCursor(28, y + 6);
        tft->print(label);
        if (i == 1) {
          tft->setCursor(28, y + 16);
          if (WifiManager_isConnected()) { char ip[20]; WifiManager_getIpString(ip, sizeof(ip)); tft->print(ip); }
          else tft->print("Not connected");
        }
        if (i == 2) tft->setCursor(28, y + 16), tft->print(AppState_getBuzzerEnabled() ? "On" : "Off");
        y += btnH + gap;
      }
      tft->fillRoundRect(20, y, 80, 28, 6, kBtn);
      tft->drawRoundRect(20, y, 80, 28, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(36, y + 6);
      tft->print("Back");
      break;
    }
    case SCREEN_EMAIL_SETTINGS: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 8);
      tft->print("Email settings");
      tft->setTextSize(1);
      char tmp[APP_STATE_EMAIL_STR_LEN];
      int rowH = 28, y = 38;
      AppState_getSmtpServer(tmp, sizeof(tmp));
      tft->setCursor(20, y); tft->print("1. SMTP server"); tft->setCursor(w/2, y); tft->print(strlen(tmp) ? tmp : "-"); y += rowH;
      AppState_getSmtpPort(tmp, sizeof(tmp));
      tft->setCursor(20, y); tft->print("2. Port"); tft->setCursor(w/2, y); tft->print(strlen(tmp) ? tmp : "587"); y += rowH;
      AppState_getSmtpUser(tmp, sizeof(tmp));
      tft->setCursor(20, y); tft->print("3. Sender email"); tft->setCursor(w/2, y); tft->print(strlen(tmp) ? tmp : "-"); y += rowH;
      AppState_getSmtpPass(tmp, sizeof(tmp));
      tft->setCursor(20, y); tft->print("4. SMTP password"); tft->setCursor(w/2, y); tft->print(strlen(tmp) ? "****" : "-"); y += rowH;
      AppState_getReportToEmail(tmp, sizeof(tmp));
      tft->setCursor(20, y); tft->print(AppState_isFieldMode() ? "5. Recipient email" : "5. Teacher email"); tft->setCursor(w/2, y); tft->print(strlen(tmp) ? tmp : "-"); y += rowH + 6;
      for (int i = 0; i < 5; i++) {
        tft->fillRoundRect(20, y, w - 40, 26, 4, kBtn);
        tft->drawRoundRect(20, y, w - 40, 26, 4, kWhite);
        tft->setTextColor(kWhite, kBtn);
        tft->setCursor(26, y + 6);
        if (i == 0) tft->print("Edit SMTP server");
        else if (i == 1) tft->print("Edit port");
        else if (i == 2) tft->print("Edit sender email");
        else if (i == 3) tft->print("Edit SMTP password");
        else tft->print(AppState_isFieldMode() ? "Edit recipient email" : "Edit teacher email");
        y += 28;
      }
      tft->fillRoundRect(20, h - 38, 80, 30, 6, kBtn);
      tft->drawRoundRect(20, h - 38, 80, 30, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(36, h - 30);
      tft->print("Back");
      break;
    }
    case SCREEN_EMAIL_FIELD_EDIT: {
      const char* titles[] = { "SMTP server", "Port", "Sender email", "SMTP password", "Report to email" };
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(1);
      tft->setCursor(20, 8);
      tft->print(titles[s_editingEmailField]);
      tft->setCursor(20, 28);
      tft->print(s_editingEmailField == 3 ? (s_editLen > 0 ? "********" : "(not set)") : (s_editLen > 0 ? s_editBuffer : "(tap keys)"));
      int cellW = (w - 56) / 10, cellH = 24, startY = 48, gap = 4;
      const char* row0 = "1234567890", * row1 = "qwertyuiop", * row2 = "asdfghjkl", * row3 = "zxcvbnm", * row4 = "@.-_";
      for (int col = 0; col < 10; col++) {
        int kx = 20 + col * (cellW + gap);
        tft->fillRoundRect(kx, startY, cellW, cellH, 4, kBtn);
        tft->drawRoundRect(kx, startY, cellW, cellH, 4, kWhite);
        char c[2] = { row0[col], '\0' };
        tft->setTextColor(kWhite, kBtn);
        tft->setCursor(kx + 2, startY + 4);
        tft->print(c);
      }
      startY += cellH + gap;
      for (int col = 0; col < 10; col++) {
        int kx = 20 + col * (cellW + gap);
        tft->fillRoundRect(kx, startY, cellW, cellH, 4, kBtn);
        tft->drawRoundRect(kx, startY, cellW, cellH, 4, kWhite);
        char c[2] = { row1[col], '\0' };
        tft->setCursor(kx + 2, startY + 4);
        tft->print(c);
      }
      startY += cellH + gap;
      for (int col = 0; col < 9; col++) {
        int kx = 20 + col * (cellW + gap);
        tft->fillRoundRect(kx, startY, cellW, cellH, 4, kBtn);
        tft->drawRoundRect(kx, startY, cellW, cellH, 4, kWhite);
        char c[2] = { row2[col], '\0' };
        tft->setCursor(kx + 2, startY + 4);
        tft->print(c);
      }
      startY += cellH + gap;
      for (int col = 0; col < 7; col++) {
        int kx = 20 + col * (cellW + gap);
        tft->fillRoundRect(kx, startY, cellW, cellH, 4, kBtn);
        tft->drawRoundRect(kx, startY, cellW, cellH, 4, kWhite);
        char c[2] = { row3[col], '\0' };
        tft->setCursor(kx + 2, startY + 4);
        tft->print(c);
      }
      startY += cellH + gap;
      for (int col = 0; col < 4; col++) {
        int kx = 20 + col * (cellW + gap);
        tft->fillRoundRect(kx, startY, cellW, cellH, 4, kBtn);
        tft->drawRoundRect(kx, startY, cellW, cellH, 4, kWhite);
        char c[2] = { row4[col], '\0' };
        tft->setTextColor(kWhite, kBtn);
        tft->setCursor(kx + 2, startY + 4);
        tft->print(c);
      }
      startY += cellH + gap + 4;
      tft->fillRoundRect(20, startY, 50, 28, 6, kAccent);
      tft->drawRoundRect(20, startY, 50, 28, 6, kWhite);
      tft->setTextColor(TFT_BLACK, kAccent);
      tft->setCursor(24, startY + 6);
      tft->print("Del");
      tft->fillRoundRect(78, startY, 60, 28, 6, kGreen);
      tft->drawRoundRect(78, startY, 60, 28, 6, kWhite);
      tft->setTextColor(TFT_BLACK, kGreen);
      tft->setCursor(90, startY + 6);
      tft->print("Save");
      tft->fillRoundRect(w - 62, 6, 48, 24, 6, kBtn);
      tft->drawRoundRect(w - 62, 6, 48, 24, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(w - 56, 10);
      tft->print("Back");
      break;
    }
    case SCREEN_ROTATION: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 14);
      tft->print("Screen orientation");
      int r = AppState_getRotation();
      int btnW = w - 40, btnH = 48, y = 60;
      tft->fillRoundRect(20, y, btnW, btnH, 8, r == 0 ? kGreen : kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(r == 0 ? TFT_BLACK : kWhite, r == 0 ? kGreen : kBtn);
      tft->setCursor(28, y + 14);
      tft->print("Portrait");
      y += 58;
      tft->fillRoundRect(20, y, btnW, btnH, 8, r == 1 ? kGreen : kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(r == 1 ? TFT_BLACK : kWhite, r == 1 ? kGreen : kBtn);
      tft->setCursor(28, y + 14);
      tft->print("Landscape");
      y += 60;
      tft->fillRoundRect(20, y, 80, 36, 6, kBtn);
      tft->drawRoundRect(20, y, 80, 36, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(40, y + 10);
      tft->print("Back");
      break;
    }
    case SCREEN_WIFI_LIST: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 8);
      tft->print("WiFi");
      tft->setTextSize(1);
      if (WifiManager_isConnected()) {
        char ssid[WIFI_SSID_LEN], ip[20];
        WifiManager_getConnectedSsid(ssid, sizeof(ssid));
        WifiManager_getIpString(ip, sizeof(ip));
        tft->setCursor(20, 34);
        tft->print("Connected: ");
        tft->print(ssid);
        tft->setCursor(20, 48);
        tft->print(ip);
      } else {
        tft->setCursor(20, 34);
        tft->print("Not connected");
      }
      int scanY = 62, btnH = 28, rowH = 30;
      tft->fillRoundRect(20, scanY, 100, 28, 6, kGreen);
      tft->drawRoundRect(20, scanY, 100, 28, 6, kWhite);
      tft->setTextColor(TFT_BLACK, kGreen);
      tft->setCursor(36, scanY + 6);
      tft->print("Scan");
      int listY = scanY + 36;
      for (int i = 0; i < s_networkCount && listY + rowH < h - 44; i++) {
        tft->setTextColor(kWhite, kBg);
        tft->setCursor(20, listY);
        tft->print(s_networks[i].ssid);
        tft->setCursor(w - 50, listY);
        tft->print(s_networks[i].rssi);
        tft->print(" dBm");
        listY += rowH;
      }
      tft->fillRoundRect(20, h - 42, 80, 34, 6, kBtn);
      tft->drawRoundRect(20, h - 42, 80, 34, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(40, h - 30);
      tft->print("Back");
      break;
    }
    case SCREEN_WIFI_PASSWORD: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(1);
      tft->setCursor(20, 10);
      tft->print("Password for:");
      tft->setCursor(20, 26);
      tft->print(s_selectedSsid);
      tft->setCursor(20, 46);
      tft->print(s_wifiPassLen > 0 ? "********" : "(tap keys below)");
      int cellW = (w - 56) / 10, cellH = 26, startY = 68, gap = 4;
      const char* row0 = "1234567890";
      const char* row1 = "qwertyuiop";
      const char* row2 = "asdfghjkl";
      const char* row3 = "zxcvbnm";
      for (int col = 0; col < 10; col++) {
        int kx = 20 + col * (cellW + gap);
        tft->fillRoundRect(kx, startY, cellW, cellH, 4, kBtn);
        tft->drawRoundRect(kx, startY, cellW, cellH, 4, kWhite);
        tft->setTextColor(kWhite, kBtn);
        tft->setTextSize(1);
        char c[2] = { row0[col], '\0' };
        tft->setCursor(kx + 2, startY + 6);
        tft->print(c);
      }
      startY += cellH + gap;
      for (int col = 0; col < 10; col++) {
        int kx = 20 + col * (cellW + gap);
        tft->fillRoundRect(kx, startY, cellW, cellH, 4, kBtn);
        tft->drawRoundRect(kx, startY, cellW, cellH, 4, kWhite);
        char c[2] = { row1[col], '\0' };
        tft->setCursor(kx + 2, startY + 6);
        tft->print(c);
      }
      startY += cellH + gap;
      for (int col = 0; col < 9; col++) {
        int kx = 20 + col * (cellW + gap);
        tft->fillRoundRect(kx, startY, cellW, cellH, 4, kBtn);
        tft->drawRoundRect(kx, startY, cellW, cellH, 4, kWhite);
        char c[2] = { row2[col], '\0' };
        tft->setCursor(kx + 2, startY + 6);
        tft->print(c);
      }
      startY += cellH + gap;
      for (int col = 0; col < 7; col++) {
        int kx = 20 + col * (cellW + gap);
        tft->fillRoundRect(kx, startY, cellW, cellH, 4, kBtn);
        tft->drawRoundRect(kx, startY, cellW, cellH, 4, kWhite);
        char c[2] = { row3[col], '\0' };
        tft->setCursor(kx + 2, startY + 6);
        tft->print(c);
      }
      startY += cellH + gap + 4;
      tft->fillRoundRect(20, startY, 60, 32, 6, kAccent);
      tft->drawRoundRect(20, startY, 60, 32, 6, kWhite);
      tft->setTextColor(TFT_BLACK, kAccent);
      tft->setCursor(26, startY + 8);
      tft->print("Del");
      tft->fillRoundRect(90, startY, 80, 32, 6, kGreen);
      tft->drawRoundRect(90, startY, 80, 32, 6, kWhite);
      tft->setTextColor(TFT_BLACK, kGreen);
      tft->setCursor(108, startY + 8);
      tft->print("Connect");
      tft->fillRoundRect(w - 70, 8, 50, 26, 6, kBtn);
      tft->drawRoundRect(w - 70, 8, 50, 26, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(w - 62, 12);
      tft->print("Back");
      break;
    }
    case SCREEN_ABOUT: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 8);
      tft->print("About SparkyCheck");
      tft->setTextSize(1);
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 34);
      tft->print("What it is");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(20, 48);
      tft->print("Portable verification coach for AS/NZS. Step-by-step tests, safety reminders, result entry with pass/fail, and reports for print or email.");
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 98);
      tft->print("Created by");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(20, 112);
      tft->print("Frank Offer 2026");
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 132);
      tft->print("Current standards");
      tft->setTextColor(kWhite, kBg);
      StandardInfo info;
      int ly = 146;
      for (int sid = 0; sid < STANDARD_COUNT; sid++) {
        Standards_getInfo((StandardId)sid, &info);
        if (!Standards_isActiveInCurrentMode((StandardId)sid)) continue;
        tft->setCursor(20, ly);
        tft->print(info.short_name);
        if (info.section && info.section[0]) { tft->print(" "); tft->print(info.section); }
        else { tft->print(" - "); tft->print(info.title); }
        ly += 14;
      }
      char rv[16];
      Standards_getRulesVersion(rv, sizeof(rv));
      tft->setCursor(20, ly + 4);
      tft->setTextColor(kAccent, kBg);
      tft->print("Rules version: ");
      tft->setTextColor(kWhite, kBg);
      tft->print(rv);
      tft->fillRoundRect(20, h - 44, 80, 36, 6, kBtn);
      tft->drawRoundRect(20, h - 44, 80, 36, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(40, h - 32);
      tft->print("Back");
      break;
    }
    case SCREEN_UPDATES: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 8);
      tft->print("Firmware updates");
      tft->setTextSize(1);

      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 34);
      tft->print("Current firmware:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(150, 34);
      tft->print(OtaUpdate_getCurrentVersion());

      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 50);
      tft->print("Auto-check:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(92, 50);
      tft->print(AppState_getOtaAutoCheckEnabled() ? "On" : "Off");

      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 64);
      tft->print("Auto-install:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(92, 64);
      tft->print(AppState_getOtaAutoInstallEnabled() ? "On" : "Off");

      char manifestUrl[APP_STATE_OTA_URL_LEN];
      OtaUpdate_getManifestUrl(manifestUrl, sizeof(manifestUrl));
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 78);
      tft->print("Manifest URL:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(20, 92);
      tft->print(manifestUrl[0] ? "Configured" : "Not configured");

      char status[OTA_STATUS_LEN];
      OtaUpdate_getLastStatus(status, sizeof(status));
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 108);
      tft->print("Status:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(20, 122);
      tft->print(status);

      char pending[OTA_VERSION_LEN];
      OtaUpdate_getPendingVersion(pending, sizeof(pending));
      if (pending[0]) {
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(20, 136);
        tft->print("Pending:");
        tft->setTextColor(kWhite, kBg);
        tft->setCursor(74, 136);
        tft->print(pending);
      }

      if (!AppState_isFieldMode()) {
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(20, 136);
        tft->print("Training sync:");
        tft->setTextColor(kWhite, kBg);
        tft->setCursor(102, 136);
        tft->print(AppState_getTrainingSyncEnabled() ? "On" : "Off");
      }

      int btnY = 150, btnH = 30, gap = 6;
      tft->fillRoundRect(20, btnY, w - 40, btnH, 6, kGreen);
      tft->drawRoundRect(20, btnY, w - 40, btnH, 6, kWhite);
      tft->setTextColor(TFT_BLACK, kGreen);
      tft->setCursor(w / 2 - 36, btnY + 8);
      tft->print("Check now");

      btnY += btnH + gap;
      uint16_t installColor = OtaUpdate_hasPendingUpdate() ? kAccent : kBtn;
      uint16_t installText = OtaUpdate_hasPendingUpdate() ? TFT_BLACK : kWhite;
      tft->fillRoundRect(20, btnY, w - 40, btnH, 6, installColor);
      tft->drawRoundRect(20, btnY, w - 40, btnH, 6, kWhite);
      tft->setTextColor(installText, installColor);
      tft->setCursor(w / 2 - 34, btnY + 8);
      tft->print("Install now");

      btnY += btnH + gap;
      int half = (w - 50) / 2;
      tft->fillRoundRect(20, btnY, half, btnH, 6, kBtn);
      tft->drawRoundRect(20, btnY, half, btnH, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(26, btnY + 8);
      tft->print("Toggle auto-check");
      tft->fillRoundRect(30 + half, btnY, half, btnH, 6, kBtn);
      tft->drawRoundRect(30 + half, btnY, half, btnH, 6, kWhite);
      tft->setCursor(36 + half, btnY + 8);
      tft->print("Toggle auto-install");

      btnY += btnH + gap;
      if (!AppState_isFieldMode()) {
        tft->fillRoundRect(20, btnY, w - 40, btnH, 6, kBtn);
        tft->drawRoundRect(20, btnY, w - 40, btnH, 6, kWhite);
        tft->setTextColor(kWhite, kBtn);
        tft->setCursor(w / 2 - 74, btnY + 8);
        tft->print("Training sync setup (PIN)");
        btnY += btnH + gap;
      }

      tft->fillRoundRect(20, btnY, 80, 26, 6, kBtn);
      tft->drawRoundRect(20, btnY, 80, 26, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(40, btnY + 6);
      tft->print("Back");
      break;
    }
    case SCREEN_TRAINING_SYNC: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 8);
      tft->print("Training sync");
      tft->setTextSize(1);

      if (AppState_isFieldMode()) {
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(20, 42);
        tft->print("This feature is only available in Training mode.");
        tft->fillRoundRect(20, h - 38, 80, 30, 6, kBtn);
        tft->drawRoundRect(20, h - 38, 80, 30, 6, kWhite);
        tft->setTextColor(kWhite, kBtn);
        tft->setCursor(36, h - 30);
        tft->print("Back");
        break;
      }

      char endpoint[APP_STATE_TRAINING_SYNC_URL_LEN];
      char cubicle[APP_STATE_TRAINING_SYNC_CUBICLE_LEN];
      char deviceId[GOOGLE_SYNC_DEVICE_ID_LEN];
      char status[GOOGLE_SYNC_STATUS_LEN];
      TrainingSyncTarget target = AppState_getTrainingSyncTarget();
      AppState_getTrainingSyncEndpoint(endpoint, sizeof(endpoint));
      AppState_getTrainingSyncCubicleId(cubicle, sizeof(cubicle));
      GoogleSync_getDeviceId(deviceId, sizeof(deviceId));
      GoogleSync_getLastStatus(status, sizeof(status));

      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 34);
      tft->print("Email report:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(96, 34);
      tft->print(AppState_getEmailReportEnabled() ? "On" : "Off");
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 48);
      tft->print("Cloud sync:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(82, 48);
      tft->print(AppState_getTrainingSyncEnabled() ? "On" : "Off");
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 62);
      tft->print("Target:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(66, 62);
      tft->print(syncTargetLabel(target));
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 76);
      tft->print("Endpoint:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(82, 76);
      tft->print(endpoint[0] ? "Configured" : "Not set");
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 90);
      tft->print("Cubicle:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(74, 90);
      tft->print(cubicle[0] ? cubicle : "(not set)");
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 104);
      tft->print("Device:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(68, 104);
      tft->print(deviceId);
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 118);
      tft->print("Status:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(20, 132);
      tft->print(status);

      int y = 152, btnH = 22, gap = 3;
      int half = (w - 50) / 2;
      tft->fillRoundRect(20, y, half, btnH, 6, kBtn);
      tft->drawRoundRect(20, y, half, btnH, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(26, y + 5);
      tft->print(AppState_getEmailReportEnabled() ? "Email: On" : "Email: Off");
      tft->fillRoundRect(30 + half, y, half, btnH, 6, kBtn);
      tft->drawRoundRect(30 + half, y, half, btnH, 6, kWhite);
      tft->setCursor(36 + half, y + 5);
      tft->print(AppState_getTrainingSyncEnabled() ? "Cloud: On" : "Cloud: Off");
      y += btnH + gap;

      tft->fillRoundRect(20, y, w - 40, btnH, 6, kBtn);
      tft->drawRoundRect(20, y, w - 40, btnH, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(28, y + 5);
      tft->print("Cycle sync target (Auto/Google/SharePoint)");
      y += btnH + gap;

      tft->fillRoundRect(20, y, w - 40, btnH, 6, kBtn);
      tft->drawRoundRect(20, y, w - 40, btnH, 6, kWhite);
      tft->setCursor(28, y + 5);
      tft->print("Edit sync endpoint URL");
      y += btnH + gap;

      tft->fillRoundRect(20, y, w - 40, btnH, 6, kBtn);
      tft->drawRoundRect(20, y, w - 40, btnH, 6, kWhite);
      tft->setCursor(28, y + 5);
      tft->print("Edit auth token (optional)");
      y += btnH + gap;

      tft->fillRoundRect(20, y, w - 40, btnH, 6, kBtn);
      tft->drawRoundRect(20, y, w - 40, btnH, 6, kWhite);
      tft->setCursor(28, y + 5);
      tft->print("Edit cubicle ID (e.g. CUB-03)");
      y += btnH + gap;

      tft->fillRoundRect(20, y, w - 40, btnH, 6, kBtn);
      tft->drawRoundRect(20, y, w - 40, btnH, 6, kWhite);
      tft->setCursor(28, y + 5);
      tft->print("Edit device ID label (e.g. CUB-03)");
      y += btnH + gap;

      tft->fillRoundRect(20, y, w - 40, btnH, 6, kGreen);
      tft->drawRoundRect(20, y, w - 40, btnH, 6, kWhite);
      tft->setTextColor(TFT_BLACK, kGreen);
      tft->setCursor(28, y + 5);
      tft->print("Send test ping");
      y += btnH + gap;

      tft->fillRoundRect(20, y, 80, btnH, 6, kBtn);
      tft->drawRoundRect(20, y, 80, btnH, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(36, y + 5);
      tft->print("Back");
      break;
    }
    case SCREEN_TRAINING_SYNC_EDIT: {
      const char* titles[] = { "Sync endpoint URL", "Auth token", "Cubicle ID", "Device ID label" };
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(1);
      tft->setCursor(20, 8);
      tft->print(titles[s_syncEditField]);
      tft->setCursor(20, 28);
      if (s_syncEditField == 1)
        tft->print(s_syncEditLen > 0 ? "********" : "(not set)");
      else
        tft->print(s_syncEditLen > 0 ? s_syncEditBuffer : "(tap keys)");

      int cellW = (w - 56) / 10, cellH = 24, startY = 48, gap = 4;
      const char* row0 = "1234567890";
      const char* row1 = "qwertyuiop";
      const char* row2 = "asdfghjkl";
      const char* row3 = "zxcvbnm";
      const char* row4 = "@.-_/:?&";

      for (int col = 0; col < 10; col++) {
        int kx = 20 + col * (cellW + gap);
        tft->fillRoundRect(kx, startY, cellW, cellH, 4, kBtn);
        tft->drawRoundRect(kx, startY, cellW, cellH, 4, kWhite);
        char c[2] = { row0[col], '\0' };
        tft->setTextColor(kWhite, kBtn);
        tft->setCursor(kx + 2, startY + 4);
        tft->print(c);
      }
      startY += cellH + gap;
      for (int col = 0; col < 10; col++) {
        int kx = 20 + col * (cellW + gap);
        tft->fillRoundRect(kx, startY, cellW, cellH, 4, kBtn);
        tft->drawRoundRect(kx, startY, cellW, cellH, 4, kWhite);
        char c[2] = { applyLetterCase(row1[col], s_syncEditUpper), '\0' };
        tft->setCursor(kx + 2, startY + 4);
        tft->print(c);
      }
      startY += cellH + gap;
      for (int col = 0; col < 9; col++) {
        int kx = 20 + col * (cellW + gap);
        tft->fillRoundRect(kx, startY, cellW, cellH, 4, kBtn);
        tft->drawRoundRect(kx, startY, cellW, cellH, 4, kWhite);
        char c[2] = { applyLetterCase(row2[col], s_syncEditUpper), '\0' };
        tft->setCursor(kx + 2, startY + 4);
        tft->print(c);
      }
      startY += cellH + gap;
      for (int col = 0; col < 7; col++) {
        int kx = 20 + col * (cellW + gap);
        tft->fillRoundRect(kx, startY, cellW, cellH, 4, kBtn);
        tft->drawRoundRect(kx, startY, cellW, cellH, 4, kWhite);
        char c[2] = { applyLetterCase(row3[col], s_syncEditUpper), '\0' };
        tft->setCursor(kx + 2, startY + 4);
        tft->print(c);
      }
      startY += cellH + gap;
      for (int col = 0; col < 8; col++) {
        int kx = 20 + col * (cellW + gap);
        tft->fillRoundRect(kx, startY, cellW, cellH, 4, kBtn);
        tft->drawRoundRect(kx, startY, cellW, cellH, 4, kWhite);
        char c[2] = { row4[col], '\0' };
        tft->setCursor(kx + 2, startY + 4);
        tft->print(c);
      }

      startY += cellH + gap + 4;
      tft->fillRoundRect(20, startY, 50, 28, 6, kAccent);
      tft->drawRoundRect(20, startY, 50, 28, 6, kWhite);
      tft->setTextColor(TFT_BLACK, kAccent);
      tft->setCursor(24, startY + 6);
      tft->print("Del");

      tft->fillRoundRect(78, startY, 44, 28, 6, kBtn);
      tft->drawRoundRect(78, startY, 44, 28, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(86, startY + 6);
      tft->print("Aa");

      tft->fillRoundRect(130, startY, 60, 28, 6, kGreen);
      tft->drawRoundRect(130, startY, 60, 28, 6, kWhite);
      tft->setTextColor(TFT_BLACK, kGreen);
      tft->setCursor(142, startY + 6);
      tft->print("Save");

      tft->fillRoundRect(w - 62, 6, 48, 24, 6, kBtn);
      tft->drawRoundRect(w - 62, 6, 48, 24, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(w - 56, 10);
      tft->print("Back");
      break;
    }
    case SCREEN_PIN_ENTER: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 12);
      tft->print("Enter PIN");
      tft->setTextSize(1);
      tft->setCursor(20, 40);
      tft->print("Minimum 4 digits (authorised users only):");
      char disp[kPinMaxLen + 1];
      int dlen = s_pinLen;
      if (dlen > kPinMaxLen) dlen = kPinMaxLen;
      for (int i = 0; i < dlen; i++) disp[i] = '*';
      disp[dlen] = '\0';
      if (dlen == 0) strcpy(disp, "(none)");
      tft->setTextSize(2);
      tft->setCursor(20, 62);
      tft->print(disp);
      /* Keypad: 1-9, then Clear 0 Confirm. Back top-right */
      int cellW = (w - 50) / 3, cellH = 38, startY = 100, gap = 6;
      const char* keys = "123456789C0E";  /* C=Clear, E=Enter/Confirm */
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
          int idx = row * 3 + col;
          int kx = 20 + col * (cellW + gap), ky = startY + row * (cellH + gap);
          uint16_t cellBg = kBtn;
          if (keys[idx] == 'C') cellBg = kAccent;
          else if (keys[idx] == 'E') cellBg = kGreen;
          tft->fillRoundRect(kx, ky, cellW, cellH, 6, cellBg);
          tft->drawRoundRect(kx, ky, cellW, cellH, 6, kWhite);
          tft->setTextColor(TFT_BLACK, cellBg);
          tft->setTextSize(1);
          if (keys[idx] == 'C') tft->setCursor(kx + cellW/2 - 12, ky + cellH/2 - 4), tft->print("Clear");
          else if (keys[idx] == 'E') tft->setCursor(kx + cellW/2 - 18, ky + cellH/2 - 4), tft->print("Confirm");
          else tft->setCursor(kx + cellW/2 - 4, ky + cellH/2 - 4), tft->print(keys[idx]);
        }
      }
      tft->fillRoundRect(w - 70, 8, 50, 28, 6, kBtn);
      tft->drawRoundRect(w - 70, 8, 50, 28, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(w - 62, 14);
      tft->print("Back");
      break;
    }
    case SCREEN_CHANGE_PIN: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 10);
      tft->print(s_changePinStep == 0 ? "New PIN" : "Confirm PIN");
      tft->setTextSize(1);
      tft->setCursor(20, 38);
      tft->print("Enter min 4 digits");
      char disp[kPinMaxLen + 1];
      int dlen = s_pinLen;
      if (dlen > kPinMaxLen) dlen = kPinMaxLen;
      for (int i = 0; i < dlen; i++) disp[i] = '*';
      disp[dlen] = '\0';
      if (dlen == 0) strcpy(disp, "(none)");
      tft->setTextSize(2);
      tft->setCursor(20, 58);
      tft->print(disp);
      int cellW = (w - 50) / 3, cellH = 36, startY = 92, gap = 6;
      const char* keys = "123456789C0E";
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
          int idx = row * 3 + col;
          int kx = 20 + col * (cellW + gap), ky = startY + row * (cellH + gap);
          uint16_t cellBg = kBtn;
          if (keys[idx] == 'C') cellBg = kAccent;
          else if (keys[idx] == 'E') cellBg = kGreen;
          tft->fillRoundRect(kx, ky, cellW, cellH, 6, cellBg);
          tft->drawRoundRect(kx, ky, cellW, cellH, 6, kWhite);
          tft->setTextColor(TFT_BLACK, cellBg);
          tft->setTextSize(1);
          if (keys[idx] == 'C') tft->setCursor(kx + cellW/2 - 12, ky + cellH/2 - 4), tft->print("Clear");
          else if (keys[idx] == 'E') tft->setCursor(kx + cellW/2 - 18, ky + cellH/2 - 4), tft->print("OK");
          else tft->setCursor(kx + cellW/2 - 4, ky + cellH/2 - 4), tft->print(keys[idx]);
        }
      }
      tft->fillRoundRect(w - 62, 8, 48, 26, 6, kBtn);
      tft->drawRoundRect(w - 62, 8, 48, 26, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(w - 56, 12);
      tft->print("Back");
      break;
    }
    default:
      break;
  }
  sparkyDisplayFlush(tft);
}

ScreenId Screens_handleTouch(SparkyTft* tft, ScreenId current, uint16_t x, uint16_t y) {
  s_handledButton = false;
  int w = getW(tft);
  int h = getH(tft);
  int ix = (int)x, iy = (int)y;

  switch (current) {
    case SCREEN_MODE_SELECT:
      if (inRect(ix, iy, 20, 80, w - 40, 56)) { s_modeSelectChoice = 0; Screens_draw(tft, current); return handled(current); }
      if (inRect(ix, iy, 20, 150, w - 40, 56)) { s_modeSelectChoice = 1; Screens_draw(tft, current); return handled(current); }
      if (inRect(ix, iy, w/2 - 60, 220, 120, 44)) {
        AppState_setMode(s_modeSelectChoice == 1 ? APP_MODE_FIELD : APP_MODE_TRAINING);
        s_trainingSettingsUnlocked = false;
        showSavedPrompt(tft, AppState_isFieldMode() ? "Mode: Field" : "Mode: Training");
        return handled(SCREEN_MAIN_MENU);
      }
      break;
    case SCREEN_MAIN_MENU: {
      bool panelWide = (w >= 700);
      int btnH = panelWide ? 64 : 44;
      int y0 = panelWide ? 88 : 76;
      int y1 = y0 + btnH + (panelWide ? 14 : 12);
      int y2 = y1 + btnH + (panelWide ? 14 : 12);
      if (inRect(ix, iy, 20, y0, w - 40, btnH)) return handled(SCREEN_TEST_SELECT);
      if (inRect(ix, iy, 20, y1, w - 40, btnH)) return handled(SCREEN_REPORT_LIST);
      if (inRect(ix, iy, 20, y2, w - 40, btnH)) {
        if (AppState_getMode() == APP_MODE_TRAINING && !s_trainingSettingsUnlocked) {
          Screens_resetPinEntry();
          Screens_setPinSuccessTarget(SCREEN_SETTINGS);
          Screens_setPinCancelTarget(SCREEN_MAIN_MENU);
          return handled(SCREEN_PIN_ENTER);
        }
        return handled(SCREEN_SETTINGS);
      }
      break;
    }
    case SCREEN_TEST_SELECT: {
      bool panelWide = (w >= 700);
      int backW = panelWide ? 90 : 48;
      int backH = panelWide ? 34 : 24;
      int backX = w - (panelWide ? 102 : 62);
      int backY = panelWide ? 10 : 8;
      if (inRect(ix, iy, backX, backY, backW, backH)) return handled(SCREEN_MAIN_MENU);
      int rowH = panelWide ? 38 : 18;
      int y0 = panelWide ? 84 : 48;
      for (int i = 0; i < VERIFY_TEST_COUNT; i++) {
        if (inRect(ix, iy, 20, y0 + i * rowH, w - 40, rowH - 2)) {
          s_selectedTestType = i;
          s_stepIndex = 0;
          s_flowPhase = 0;
          s_resultValue = 0.0f;
          s_resultIsSheathedHeating = false;
          s_resultLabel = NULL;
          s_resultUnit = NULL;
          s_resultClause = NULL;
          resetResultEntryInput();
          s_testStartedMs = 0;
          s_testCompletedMs = 0;
          if (AppState_getMode() == APP_MODE_TRAINING) {
            s_studentInputLen = 0;
            s_studentInput[0] = '\0';
            return handled(SCREEN_STUDENT_ID);
          }
          return handled(SCREEN_TEST_FLOW);
        }
      }
      break;
    }
    case SCREEN_STUDENT_ID: {
      if (inRect(ix, iy, w - 62, 8, 48, 24)) return handled(SCREEN_TEST_SELECT);
      int cellW = (w - 50) / 3, cellH = 34, startY = 82, gap = 6;
      const char* keys = "123456789D0E";
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
          int idx = row * 3 + col;
          int kx = 20 + col * (cellW + gap), ky = startY + row * (cellH + gap);
          if (!inRect(ix, iy, kx, ky, cellW, cellH)) continue;
          char k = keys[idx];
          if (k == 'D') {
            if (s_studentInputLen > 0) s_studentInput[--s_studentInputLen] = '\0';
            Screens_draw(tft, current);
            return handled(current);
          }
          if (k == 'E') {
            if (!normalizeStudentId(s_studentInput, s_studentId, sizeof(s_studentId))) {
              Buzzer_beepFail();
              Screens_draw(tft, current);
              return handled(current);
            }
            GoogleSync_resetSession();
            s_testStartedMs = millis();
            s_testCompletedMs = 0;
            syncTrainingFlowEvent("test_started", false, nullptr);
            return handled(SCREEN_TEST_FLOW);
          }
          if (k >= '0' && k <= '9' && s_studentInputLen < APP_STATE_DEVICE_ID_LEN - 2) {
            s_studentInput[s_studentInputLen++] = k;
            s_studentInput[s_studentInputLen] = '\0';
            Screens_draw(tft, current);
            return handled(current);
          }
        }
      }
      break;
    }
    case SCREEN_TEST_FLOW: {
      int count = VerificationSteps_getStepCount((VerifyTestId)s_selectedTestType);
      if (inRect(ix, iy, w - 70, 6, 50, 24)) {
        if (s_flowPhase == 1) {
          s_flowPhase = 0;
          s_stepIndex = (count > 0) ? count - 1 : 0;
          s_testCompletedMs = 0;
          syncTrainingFlowEvent("step_back", false, nullptr);
          Screens_draw(tft, current);
          return handled(current);
        }
        if (s_stepIndex > 0) {
          s_stepIndex--;
          syncTrainingFlowEvent("step_back", false, nullptr);
          Screens_draw(tft, current);
          return handled(current);
        }
        syncTrainingFlowEvent("test_cancelled", false, nullptr);
        resetResultEntryInput();
        return handled(SCREEN_TEST_SELECT);
      }
      if (s_flowPhase == 1) {
        if (inRect(ix, iy, 20, h - 52, w - 40, 44)) {
          ReportGenerator_setStudentId(AppState_getMode() == APP_MODE_TRAINING ? s_studentId : "");
          ReportGenerator_begin(nullptr);
          char valBuf[24];
          if (s_resultUnit && s_resultUnit[0])
            snprintf(valBuf, sizeof(valBuf), "%.3f", (double)s_resultValue);
          else
            snprintf(valBuf, sizeof(valBuf), "Verified");
          ReportGenerator_addResult(s_resultLabel ? s_resultLabel : "Test", valBuf, s_resultUnit ? s_resultUnit : "", s_resultPass, s_resultClause ? s_resultClause : "");
          ReportGenerator_end();
          char base[48];
          ReportGenerator_getLastBasename(base, sizeof(base));
          syncTrainingFlowEvent("session_saved", true, base);
          Screens_setReportSavedBasename(base);
          s_flowPhase = 0;
          s_stepIndex = 0;
          s_resultValue = 0.0f;
          s_resultIsSheathedHeating = false;
          s_resultLabel = NULL;
          s_resultUnit = NULL;
          s_resultClause = NULL;
          resetResultEntryInput();
          return handled(SCREEN_REPORT_SAVED);
        }
        break;
      }
      if (s_stepIndex >= count) break;
      VerifyStep step;
      VerificationSteps_getStep((VerifyTestId)s_selectedTestType, s_stepIndex, &step);
      if (step.type == STEP_SAFETY) {
        if (inRect(ix, iy, 20, h - 54, w - 40, 44)) {
          s_stepIndex++;
          if (s_stepIndex >= count) {
            s_flowPhase = 1;
            s_resultPass = true;
            s_resultLabel = VerificationSteps_getTestName((VerifyTestId)s_selectedTestType);
            s_resultUnit = "";
            VerificationSteps_getStep((VerifyTestId)s_selectedTestType, count - 1, &step);
            s_resultClause = step.clause;
            s_testCompletedMs = millis();
            syncTrainingFlowEvent("result_confirmed", true, nullptr);
          } else {
            syncTrainingFlowEvent("step_next", false, nullptr);
          }
          Screens_draw(tft, current);
          return handled(current);
        }
      } else if (step.type == STEP_VERIFY_YESNO) {
        int half = (w - 50) / 2, btnY = h - 54;
        if (inRect(ix, iy, 20, btnY, half, 44)) {
          if (s_stepIndex == 2 && s_selectedTestType == (int)VERIFY_INSULATION) s_resultIsSheathedHeating = true;
          s_stepIndex++;
          if (s_stepIndex >= count) {
            s_flowPhase = 1;
            s_resultPass = true;
            s_resultLabel = VerificationSteps_getTestName((VerifyTestId)s_selectedTestType);
            s_resultUnit = "";
            VerificationSteps_getStep((VerifyTestId)s_selectedTestType, count - 1, &step);
            s_resultClause = step.clause;
            s_testCompletedMs = millis();
            syncTrainingFlowEvent("result_confirmed", true, nullptr);
          } else {
            syncTrainingFlowEvent("step_next", false, nullptr);
          }
          Screens_draw(tft, current);
          return handled(current);
        }
        if (inRect(ix, iy, 30 + half, btnY, half, 44)) { /* No – do not advance, or could advance with fail */ }
      } else if (step.type == STEP_INFO) {
        if (inRect(ix, iy, 20, h - 54, w - 40, 44)) {
          s_stepIndex++;
          if (s_stepIndex >= count) {
            s_flowPhase = 1;
            s_resultPass = true;
            s_resultLabel = VerificationSteps_getTestName((VerifyTestId)s_selectedTestType);
            s_resultUnit = "";
            VerificationSteps_getStep((VerifyTestId)s_selectedTestType, count - 1, &step);
            s_resultClause = step.clause;
            s_testCompletedMs = millis();
            syncTrainingFlowEvent("result_confirmed", true, nullptr);
          } else {
            syncTrainingFlowEvent("step_next", false, nullptr);
          }
          Screens_draw(tft, current);
          return handled(current);
        }
      } else if (step.type == STEP_RESULT_ENTRY) {
        ensureResultEntryInputState(step.resultKind);
        const ResultUnitOption* units = nullptr;
        int unitCount = 0;
        int defaultIdx = 0;
        getResultUnitOptions(step.resultKind, &units, &unitCount, &defaultIdx);
        if (s_resultInputUnitIdx < 0 || s_resultInputUnitIdx >= unitCount) s_resultInputUnitIdx = defaultIdx;

        if (inRect(ix, iy, w - 74, 96, 54, 20)) {
          if (s_resultInputLen > 0) s_resultInput[--s_resultInputLen] = '\0';
          Screens_draw(tft, current);
          return handled(current);
        }

        int uy = 120, uh = 18, uGap = 4;
        int uw = (w - 40 - (unitCount - 1) * uGap) / unitCount;
        for (int i = 0; i < unitCount; i++) {
          int ux = 20 + i * (uw + uGap);
          if (inRect(ix, iy, ux, uy, uw, uh)) {
            s_resultInputUnitIdx = i;
            Screens_draw(tft, current);
            return handled(current);
          }
        }

        const char* keys[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "OK" };
        int cellW = (w - 50) / 3, cellH = 20, gap = 6, startX = 20, startY = 142;
        for (int row = 0; row < 4; row++) {
          for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            int kx = startX + col * (cellW + gap), ky = startY + row * (cellH + gap);
            if (!inRect(ix, iy, kx, ky, cellW, cellH)) continue;
            const char* key = keys[idx];
            if (strcmp(key, "OK") == 0) {
              if (s_resultInputLen <= 0) {
                Buzzer_beepFail();
                showPrompt(tft, "Enter a value", "before confirming", kRed);
                Screens_draw(tft, current);
                return handled(current);
              }
              char* endptr = nullptr;
              float rawValue = strtof(s_resultInput, &endptr);
              if (endptr == s_resultInput || !endptr || *endptr != '\0' || rawValue < 0.0f) {
                Buzzer_beepFail();
                showPrompt(tft, "Invalid value", "Try again", kRed);
                Screens_draw(tft, current);
                return handled(current);
              }
              float canonicalValue = rawValue * units[s_resultInputUnitIdx].toCanonical;
              VerifyResultKind kind = step.resultKind;
              if (kind == RESULT_IR_MOHM && s_resultIsSheathedHeating) kind = RESULT_IR_MOHM_SHEATHED;
              if (kind == RESULT_RCD_REQUIRED_MAX_MS) {
                s_rcdRequiredMaxMs = canonicalValue;
                s_stepIndex++;
                s_resultInputKind = RESULT_NONE;
                s_resultInputLen = 0;
                s_resultInput[0] = '\0';
                if (s_stepIndex >= count) {
                  s_resultPass = true;
                  s_resultValue = rawValue;
                  s_resultLabel = step.resultLabel;
                  s_resultUnit = units[s_resultInputUnitIdx].label;
                  s_resultClause = VerificationSteps_getClauseForResult(step.resultKind);
                  s_flowPhase = 1;
                  s_testCompletedMs = millis();
                  syncTrainingFlowEvent("result_confirmed", true, nullptr);
                  if (AppState_getBuzzerEnabled()) Buzzer_beepPass();
                  Screens_draw(tft, current);
                } else {
                  syncTrainingFlowEvent("step_next", false, nullptr);
                  showSavedPrompt(tft, "Required max saved");
                  Screens_draw(tft, current);
                }
                return handled(current);
              }
              if (kind == RESULT_RCD_MS && s_rcdRequiredMaxMs > 0.0f)
                s_resultPass = (canonicalValue >= 0.0f && canonicalValue <= s_rcdRequiredMaxMs);
              else
                s_resultPass = VerificationSteps_validateResult(kind, canonicalValue, s_resultIsSheathedHeating);
              s_resultValue = rawValue;
              s_resultLabel = step.resultLabel;
              s_resultUnit = units[s_resultInputUnitIdx].label;
              s_resultClause = VerificationSteps_getClauseForResult(step.resultKind);
              s_flowPhase = 1;
              s_testCompletedMs = millis();
              syncTrainingFlowEvent("result_confirmed", true, nullptr);
              if (AppState_getBuzzerEnabled()) { if (s_resultPass) Buzzer_beepPass(); else Buzzer_beepFail(); }
              Screens_draw(tft, current);
              return handled(current);
            } else if (strcmp(key, ".") == 0) {
              if (!strchr(s_resultInput, '.')) {
                if (s_resultInputLen == 0 && s_resultInputLen < (int)sizeof(s_resultInput) - 2) {
                  s_resultInput[s_resultInputLen++] = '0';
                }
                if (s_resultInputLen < (int)sizeof(s_resultInput) - 1) {
                  s_resultInput[s_resultInputLen++] = '.';
                  s_resultInput[s_resultInputLen] = '\0';
                }
              }
            } else {
              if (s_resultInputLen < (int)sizeof(s_resultInput) - 1) {
                s_resultInput[s_resultInputLen++] = key[0];
                s_resultInput[s_resultInputLen] = '\0';
              }
            }
            Screens_draw(tft, current);
            return handled(current);
          }
        }
      }
      break;
    }
    case SCREEN_REPORT_SAVED:
      if (inRect(ix, iy, w/2 - 50, h - 56, 100, 44)) return handled(SCREEN_MAIN_MENU);
      break;
    case SCREEN_REPORT_LIST:
      if (inRect(ix, iy, 20, h - 52, 100, 40)) return handled(SCREEN_MAIN_MENU);
      break;
    case SCREEN_SETTINGS: {
      if (AppState_getMode() == APP_MODE_TRAINING && !s_trainingSettingsUnlocked) {
        Screens_resetPinEntry();
        Screens_setPinSuccessTarget(SCREEN_SETTINGS);
        Screens_setPinCancelTarget(SCREEN_MAIN_MENU);
        return handled(SCREEN_PIN_ENTER);
      }
      int btnH = 30, gap = 2, y = 42;
      bool field = AppState_isFieldMode();
      int nRows = field ? 7 : 8;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_ROTATION);
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_WIFI_LIST);
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) {
        bool enabled = !AppState_getBuzzerEnabled();
        AppState_setBuzzerEnabled(enabled);
        Screens_draw(tft, current);
        showSavedPrompt(tft, enabled ? "Buzzer: ON" : "Buzzer: OFF");
        Screens_draw(tft, current);
        return handled(current);
      }
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_ABOUT);
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_UPDATES);
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_EMAIL_SETTINGS);
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) {
        Screens_draw(tft, current);
        return handled(current);
      }
      y += btnH + gap;
      if (!field && inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_CHANGE_PIN);
      { int backY = 42 + nRows * (btnH + gap);
        if (inRect(ix, iy, 20, backY, 80, 28)) {
          s_trainingSettingsUnlocked = false;
          return handled(SCREEN_MAIN_MENU);
        } }
      break;
    }
    case SCREEN_ROTATION:
      if (inRect(ix, iy, 20, 60, w - 40, 48)) {
        AppState_setRotation(0);
        showSavedPrompt(tft, "Display: Portrait");
        return handled(SCREEN_SETTINGS);
      }
      if (inRect(ix, iy, 20, 118, w - 40, 48)) {
        AppState_setRotation(1);
        showSavedPrompt(tft, "Display: Landscape");
        return handled(SCREEN_SETTINGS);
      }
      if (inRect(ix, iy, 20, 178, 80, 36)) return handled(SCREEN_SETTINGS);
      break;
    case SCREEN_WIFI_LIST: {
      if (inRect(ix, iy, 20, 62, 100, 28)) {
        s_networkCount = WifiManager_scan(s_networks, WIFI_MAX_SSIDS);
        Screens_draw(tft, current);
        return handled(current);
      }
      int listY = 98, rowH = 30;
      for (int i = 0; i < s_networkCount && listY + rowH < h - 44; i++) {
        if (inRect(ix, iy, 20, listY, w - 40, rowH)) {
          strncpy(s_selectedSsid, s_networks[i].ssid, WIFI_SSID_LEN - 1);
          s_selectedSsid[WIFI_SSID_LEN - 1] = '\0';
          s_wifiPassLen = 0;
          s_wifiPass[0] = '\0';
          return handled(SCREEN_WIFI_PASSWORD);
        }
        listY += rowH;
      }
      if (inRect(ix, iy, 20, h - 42, 80, 34)) return handled(SCREEN_SETTINGS);
      break;
    }
    case SCREEN_WIFI_PASSWORD: {
      if (inRect(ix, iy, w - 70, 8, 50, 26)) {
        s_selectedSsid[0] = '\0';
        s_wifiPassLen = 0;
        return handled(SCREEN_WIFI_LIST);
      }
      int cellW = (w - 56) / 10, cellH = 26, startY = 68, gap = 4;
      const char* rows[] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };
      int cols[] = { 10, 10, 9, 7 };
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < cols[row]; col++) {
          int kx = 20 + col * (cellW + gap), ky = startY + row * (cellH + gap);
          if (inRect(ix, iy, kx, ky, cellW, cellH) && s_wifiPassLen < WIFI_PASS_LEN - 1) {
            char c = rows[row][col];
            s_wifiPass[s_wifiPassLen++] = c;
            s_wifiPass[s_wifiPassLen] = '\0';
            Screens_draw(tft, current);
            return handled(current);
          }
        }
      }
      int ctrlY = startY + 4 * (cellH + gap) + 4;
      if (inRect(ix, iy, 20, ctrlY, 60, 32)) {
        if (s_wifiPassLen > 0) s_wifiPass[--s_wifiPassLen] = '\0';
        Screens_draw(tft, current);
        return handled(current);
      }
      if (inRect(ix, iy, 90, ctrlY, 80, 32)) {
        if (WifiManager_connect(s_selectedSsid, s_wifiPassLen > 0 ? s_wifiPass : "")) {
          Buzzer_beepPass();
          showPrompt(tft, "Wi-Fi connected", s_selectedSsid, kGreen);
        } else {
          Buzzer_beepFail();
          showPrompt(tft, "Wi-Fi connect failed", "Check password", kRed);
        }
        s_selectedSsid[0] = '\0';
        s_wifiPassLen = 0;
        return handled(SCREEN_WIFI_LIST);
      }
      break;
    }
    case SCREEN_ABOUT:
      if (inRect(ix, iy, 20, h - 44, 80, 36)) return handled(SCREEN_SETTINGS);
      break;
    case SCREEN_UPDATES: {
      int btnY = 150, btnH = 30, gap = 6;
      if (inRect(ix, iy, 20, btnY, w - 40, btnH)) {
        OtaUpdate_checkNow();
        Screens_draw(tft, current);
        return handled(current);
      }
      btnY += btnH + gap;
      if (inRect(ix, iy, 20, btnY, w - 40, btnH)) {
        if (OtaUpdate_hasPendingUpdate()) OtaUpdate_installPending();
        Screens_draw(tft, current);
        return handled(current);
      }
      btnY += btnH + gap;
      int half = (w - 50) / 2;
      if (inRect(ix, iy, 20, btnY, half, btnH)) {
        bool enabled = !AppState_getOtaAutoCheckEnabled();
        AppState_setOtaAutoCheckEnabled(enabled);
        Screens_draw(tft, current);
        showSavedPrompt(tft, enabled ? "Auto-check: ON" : "Auto-check: OFF");
        Screens_draw(tft, current);
        return handled(current);
      }
      if (inRect(ix, iy, 30 + half, btnY, half, btnH)) {
        bool enabled = !AppState_getOtaAutoInstallEnabled();
        AppState_setOtaAutoInstallEnabled(enabled);
        Screens_draw(tft, current);
        showSavedPrompt(tft, enabled ? "Auto-install: ON" : "Auto-install: OFF");
        Screens_draw(tft, current);
        return handled(current);
      }
      btnY += btnH + gap;
      if (!AppState_isFieldMode() && inRect(ix, iy, 20, btnY, w - 40, btnH)) {
        Screens_resetPinEntry();
        Screens_setPinSuccessTarget(SCREEN_TRAINING_SYNC);
        Screens_setPinCancelTarget(SCREEN_UPDATES);
        return handled(SCREEN_PIN_ENTER);
      }
      if (!AppState_isFieldMode()) btnY += btnH + gap;
      if (inRect(ix, iy, 20, btnY, 80, 26)) return handled(SCREEN_SETTINGS);
      break;
    }
    case SCREEN_TRAINING_SYNC: {
      if (AppState_isFieldMode()) {
        if (inRect(ix, iy, 20, h - 38, 80, 30)) return handled(SCREEN_UPDATES);
        break;
      }
      int y = 152, btnH = 22, gap = 3;
      int half = (w - 50) / 2;
      if (inRect(ix, iy, 20, y, half, btnH)) {
        bool enabled = !AppState_getEmailReportEnabled();
        AppState_setEmailReportEnabled(enabled);
        Screens_draw(tft, current);
        showSavedPrompt(tft, enabled ? "Email reports: ON" : "Email reports: OFF");
        Screens_draw(tft, current);
        return handled(current);
      }
      if (inRect(ix, iy, 30 + half, y, half, btnH)) {
        bool enabled = !AppState_getTrainingSyncEnabled();
        AppState_setTrainingSyncEnabled(enabled);
        Screens_draw(tft, current);
        showSavedPrompt(tft, enabled ? "Cloud sync: ON" : "Cloud sync: OFF");
        Screens_draw(tft, current);
        return handled(current);
      }
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) {
        TrainingSyncTarget t = AppState_getTrainingSyncTarget();
        t = (TrainingSyncTarget)(((int)t + 1) % 3);
        AppState_setTrainingSyncTarget(t);
        Screens_draw(tft, current);
        showSavedPrompt(tft, syncTargetLabel(t));
        Screens_draw(tft, current);
        return handled(current);
      }
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) { loadSyncEditField(0); return handled(SCREEN_TRAINING_SYNC_EDIT); }
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) { loadSyncEditField(1); return handled(SCREEN_TRAINING_SYNC_EDIT); }
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) { loadSyncEditField(2); return handled(SCREEN_TRAINING_SYNC_EDIT); }
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) { loadSyncEditField(3); return handled(SCREEN_TRAINING_SYNC_EDIT); }
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) {
        if (GoogleSync_sendPing()) Buzzer_beepPass();
        else Buzzer_beepFail();
        Screens_draw(tft, current);
        return handled(current);
      }
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, 80, btnH)) return handled(SCREEN_UPDATES);
      break;
    }
    case SCREEN_TRAINING_SYNC_EDIT: {
      if (inRect(ix, iy, w - 62, 6, 48, 24)) return handled(SCREEN_TRAINING_SYNC);
      int cellW = (w - 56) / 10, cellH = 24, startY = 48, gap = 4;
      const char* rows[] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm", "@.-_/:?&" };
      int cols[] = { 10, 10, 9, 7, 8 };
      int maxLen = syncEditMaxLen();
      for (int row = 0; row < 5; row++) {
        for (int col = 0; col < cols[row]; col++) {
          int kx = 20 + col * (cellW + gap), ky = startY + row * (cellH + gap);
          if (!inRect(ix, iy, kx, ky, cellW, cellH) || s_syncEditLen >= maxLen - 1) continue;
          char c = rows[row][col];
          if (row >= 1 && row <= 3) c = applyLetterCase(c, s_syncEditUpper);
          s_syncEditBuffer[s_syncEditLen++] = c;
          s_syncEditBuffer[s_syncEditLen] = '\0';
          Screens_draw(tft, current);
          return handled(current);
        }
      }
      int ctrlY = startY + 5 * (cellH + gap) + 4;
      if (inRect(ix, iy, 20, ctrlY, 50, 28)) {
        if (s_syncEditLen > 0) s_syncEditBuffer[--s_syncEditLen] = '\0';
        Screens_draw(tft, current);
        return handled(current);
      }
      if (inRect(ix, iy, 78, ctrlY, 44, 28)) {
        s_syncEditUpper = !s_syncEditUpper;
        Screens_draw(tft, current);
        return handled(current);
      }
      if (inRect(ix, iy, 130, ctrlY, 60, 28)) {
        const char* detail = "Setting updated";
        if (s_syncEditField == 0) AppState_setTrainingSyncEndpoint(s_syncEditBuffer);
        else if (s_syncEditField == 1) AppState_setTrainingSyncToken(s_syncEditBuffer);
        else if (s_syncEditField == 2) AppState_setTrainingSyncCubicleId(s_syncEditBuffer);
        else AppState_setDeviceIdOverride(s_syncEditBuffer);
        if (s_syncEditField == 0) detail = "Endpoint saved";
        else if (s_syncEditField == 1) detail = "Token saved";
        else if (s_syncEditField == 2) detail = "Cubicle saved";
        else detail = "Device label saved";
        showSavedPrompt(tft, detail);
        return handled(SCREEN_TRAINING_SYNC);
      }
      break;
    }
    case SCREEN_EMAIL_SETTINGS: {
      int editY = 38 + 5 * 28 + 6;
      for (int i = 0; i < 5; i++) {
        if (inRect(ix, iy, 20, editY + i * 28, w - 40, 26)) {
          s_editingEmailField = i;
          s_editLen = 0;
          s_editBuffer[0] = '\0';
          if (i == 0) AppState_getSmtpServer(s_editBuffer, sizeof(s_editBuffer));
          else if (i == 1) AppState_getSmtpPort(s_editBuffer, sizeof(s_editBuffer));
          else if (i == 2) AppState_getSmtpUser(s_editBuffer, sizeof(s_editBuffer));
          else if (i == 3) AppState_getSmtpPass(s_editBuffer, sizeof(s_editBuffer));
          else AppState_getReportToEmail(s_editBuffer, sizeof(s_editBuffer));
          s_editLen = strlen(s_editBuffer);
          s_editBuffer[s_editLen] = '\0';
          return handled(SCREEN_EMAIL_FIELD_EDIT);
        }
      }
      if (inRect(ix, iy, 20, h - 38, 80, 30)) return handled(SCREEN_SETTINGS);
      break;
    }
    case SCREEN_EMAIL_FIELD_EDIT: {
      if (inRect(ix, iy, w - 62, 6, 48, 24)) return handled(SCREEN_EMAIL_SETTINGS);
      int cellW = (w - 56) / 10, cellH = 24, startY = 48, gap = 4;
      const char* rows[] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm", "@.-_" };
      int cols[] = { 10, 10, 9, 7, 4 };
      for (int row = 0; row < 5; row++) {
        for (int col = 0; col < cols[row]; col++) {
          int kx = 20 + col * (cellW + gap), ky = startY + row * (cellH + gap);
          if (inRect(ix, iy, kx, ky, cellW, cellH) && s_editLen < (int)(APP_STATE_EMAIL_STR_LEN - 1)) {
            char c = rows[row][col];
            s_editBuffer[s_editLen++] = c;
            s_editBuffer[s_editLen] = '\0';
            Screens_draw(tft, current);
            return handled(current);
          }
        }
      }
      int ctrlY = startY + 5 * (cellH + gap) + 4;
      if (inRect(ix, iy, 20, ctrlY, 50, 28)) {
        if (s_editLen > 0) s_editBuffer[--s_editLen] = '\0';
        Screens_draw(tft, current);
        return handled(current);
      }
      if (inRect(ix, iy, 78, ctrlY, 60, 28)) {
        const char* detail = "Email setting saved";
        if (s_editingEmailField == 0) AppState_setSmtpServer(s_editBuffer);
        else if (s_editingEmailField == 1) AppState_setSmtpPort(s_editBuffer);
        else if (s_editingEmailField == 2) AppState_setSmtpUser(s_editBuffer);
        else if (s_editingEmailField == 3) AppState_setSmtpPass(s_editBuffer);
        else AppState_setReportToEmail(s_editBuffer);
        if (s_editingEmailField == 0) detail = "SMTP server saved";
        else if (s_editingEmailField == 1) detail = "SMTP port saved";
        else if (s_editingEmailField == 2) detail = "SMTP user saved";
        else if (s_editingEmailField == 3) detail = "SMTP pass saved";
        else detail = "Report email saved";
        showSavedPrompt(tft, detail);
        return handled(SCREEN_EMAIL_SETTINGS);
      }
      break;
    }
    case SCREEN_PIN_ENTER: {
      if (inRect(ix, iy, w - 70, 8, 50, 28)) {
        Screens_resetPinEntry();
        return handled(s_pinCancelTarget);
      }
      int cellW = (w - 50) / 3, cellH = 38, startY = 100, gap = 6;
      const char* keys = "123456789C0E";
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
          int idx = row * 3 + col;
          int kx = 20 + col * (cellW + gap), ky = startY + row * (cellH + gap);
          if (!inRect(ix, iy, kx, ky, cellW, cellH)) continue;
          if (keys[idx] == 'C') {
            Screens_resetPinEntry();
            Screens_draw(tft, current);
            return handled(current);
          }
          if (keys[idx] == 'E') {
            if (s_pinLen < kPinMinLen) break;
            uint32_t pin = 0;
            for (int i = 0; i < s_pinLen; i++)
              pin = pin * 10 + (s_pinDigits[i] - '0');
            if (AppState_checkPin(pin)) {
              Screens_resetPinEntry();
              if (s_pinSuccessTarget == SCREEN_SETTINGS) s_trainingSettingsUnlocked = true;
              if (s_pinSuccessTarget == SCREEN_MODE_SELECT)
                Screens_setModeSelectChoice(AppState_getMode() == APP_MODE_FIELD ? 1 : 0);
              return handled(s_pinSuccessTarget);
            } else {
              Buzzer_beepFail();
              s_pinFailAttempts++;
              s_pinLen = 0;
              s_pinDigits[0] = '\0';
              bool bootModeEntry = (s_pinSuccessTarget == SCREEN_MODE_SELECT && s_pinCancelTarget == SCREEN_MAIN_MENU);
              if (bootModeEntry && s_pinFailAttempts >= kPinMaxAttempts) {
                tft->fillScreen(kBg);
                tft->setTextColor(kWhite, kBg);
                tft->setTextSize(1);
                tft->setCursor(20, h / 2 - 12);
                tft->print("3 unsuccesful login attempts,");
                tft->setCursor(20, h / 2 + 4);
                tft->print("continuing boot process");
                delay(3000);
                Screens_resetPinEntry();
                return handled(SCREEN_MAIN_MENU);
              }
              Screens_draw(tft, current);
              return handled(current);
            }
          }
          if (keys[idx] >= '0' && keys[idx] <= '9' && s_pinLen < kPinMaxLen) {
            s_pinDigits[s_pinLen++] = keys[idx];
            s_pinDigits[s_pinLen] = '\0';
            Screens_draw(tft, current);
            return handled(current);
          }
        }
      }
      break;
    }
    case SCREEN_CHANGE_PIN: {
      if (inRect(ix, iy, w - 62, 8, 48, 26)) {
        s_changePinStep = 0; s_pinLen = 0; s_pinDigits[0] = '\0'; s_newPinBuf[0] = '\0'; s_newPinLen = 0;
        return handled(SCREEN_SETTINGS);
      }
      int cellW = (w - 50) / 3, cellH = 36, startY = 92, gap = 6;
      const char* keys = "123456789C0E";
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
          int idx = row * 3 + col;
          int kx = 20 + col * (cellW + gap), ky = startY + row * (cellH + gap);
          if (!inRect(ix, iy, kx, ky, cellW, cellH)) continue;
          if (keys[idx] == 'C') {
            s_pinLen = 0; s_pinDigits[0] = '\0';
            Screens_draw(tft, current);
            return handled(current);
          }
          if (keys[idx] == 'E') {
            if (s_pinLen < kPinMinLen) break;
            if (s_changePinStep == 0) {
              memcpy(s_newPinBuf, s_pinDigits, s_pinLen);
              s_newPinBuf[s_pinLen] = '\0';
              s_newPinLen = s_pinLen;
              s_changePinStep = 1;
              s_pinLen = 0; s_pinDigits[0] = '\0';
              Screens_draw(tft, current);
              return handled(current);
            } else {
              bool match = (s_pinLen == s_newPinLen && s_pinLen >= kPinMinLen &&
                            strncmp(s_pinDigits, s_newPinBuf, s_newPinLen) == 0);
              if (match) {
                uint32_t pin = 0;
                for (int i = 0; i < s_newPinLen; i++) pin = pin * 10 + (s_newPinBuf[i] - '0');
                AppState_setPin(pin);
                Buzzer_beepPass();
                showSavedPrompt(tft, "PIN updated");
              } else {
                Buzzer_beepFail();
                showPrompt(tft, "PIN not changed", "Entries did not match", kRed);
              }
              s_changePinStep = 0; s_pinLen = 0; s_pinDigits[0] = '\0'; s_newPinBuf[0] = '\0'; s_newPinLen = 0;
              return handled(SCREEN_SETTINGS);
            }
          }
          if (keys[idx] >= '0' && keys[idx] <= '9' && s_pinLen < kPinMaxLen) {
            s_pinDigits[s_pinLen++] = keys[idx];
            s_pinDigits[s_pinLen] = '\0';
            Screens_draw(tft, current);
            return handled(current);
          }
        }
      }
      break;
    }
    default:
      break;
  }
  /* No button hit – do not set s_handledButton */
  return current;
}
