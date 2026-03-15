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
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <ctype.h>
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

/* PIN entry: where to go after correct PIN */
static ScreenId s_pinSuccessTarget = SCREEN_MODE_SELECT;

/* PIN entry state (5-digit PIN) */
static char s_pinDigits[6] = "";
static int s_pinLen = 0;

/* Change PIN: step 0 = enter new, step 1 = confirm; buffers for comparison */
static int s_changePinStep = 0;
static char s_newPinBuf[6] = "";
static int s_newPinLen = 0;

/* Email settings edit: which field (0=server,1=port,2=user,3=pass,4=reportTo) and buffer */
static int s_editingEmailField = 0;
static char s_editBuffer[APP_STATE_EMAIL_STR_LEN] = "";
static int s_editLen = 0;

/* Training sync edit state: field 0=url, 1=token, 2=cubicle */
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

static int getW(TFT_eSPI* tft) { return tft->width(); }
static int getH(TFT_eSPI* tft) { return tft->height(); }

static bool s_handledButton = false;
static ScreenId handled(ScreenId id) { s_handledButton = true; return id; }

bool Screens_didHandleButton(void) {
  return s_handledButton;
}

static bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static int syncEditMaxLen(void) {
  if (s_syncEditField == 1) return APP_STATE_TRAINING_SYNC_TOKEN_LEN;
  if (s_syncEditField == 2) return APP_STATE_TRAINING_SYNC_CUBICLE_LEN;
  return APP_STATE_TRAINING_SYNC_URL_LEN;
}

static void loadSyncEditField(int field) {
  s_syncEditField = field;
  s_syncEditUpper = false;
  s_syncEditLen = 0;
  s_syncEditBuffer[0] = '\0';
  if (field == 0) AppState_getTrainingSyncEndpoint(s_syncEditBuffer, sizeof(s_syncEditBuffer));
  else if (field == 1) AppState_getTrainingSyncToken(s_syncEditBuffer, sizeof(s_syncEditBuffer));
  else AppState_getTrainingSyncCubicleId(s_syncEditBuffer, sizeof(s_syncEditBuffer));
  s_syncEditLen = strlen(s_syncEditBuffer);
  if (s_syncEditLen >= syncEditMaxLen()) s_syncEditLen = syncEditMaxLen() - 1;
  s_syncEditBuffer[s_syncEditLen] = '\0';
}

static char applyLetterCase(char c, bool upper) {
  if (!upper) return c;
  if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
  return c;
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
  gs.step_title = stepTitle;
  gs.step_index = stepIndex;
  gs.step_count = count;
  gs.has_result = include_result;
  gs.session_id = "";
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

void Screens_draw(TFT_eSPI* tft, ScreenId id) {
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
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 16);
      tft->print("SparkyCheck");
      tft->setTextSize(1);
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 44);
      tft->print(AppState_isFieldMode() ? "Field mode" : "Training mode");
      int btnW = w - 40, btnH = 44, y = 76;
      tft->fillRoundRect(20, y, btnW, btnH, 8, kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(36, y + 12);
      tft->print("Start verification");
      y += 56;
      tft->fillRoundRect(20, y, btnW, btnH, 8, kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(36, y + 12);
      tft->print("View reports");
      y += 56;
      tft->fillRoundRect(20, y, btnW, btnH, 8, kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(36, y + 12);
      tft->print("Settings");
      break;
    }
    case SCREEN_TEST_SELECT: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 10);
      tft->print("Select test");
      tft->setTextSize(1);
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 38);
      { char scope[96];
        Standards_getVerificationScopeLine(scope, sizeof(scope));
        tft->print(scope);
      }
      int rowH = 36, y = 56;
      for (int i = 0; i < VERIFY_TEST_COUNT; i++) {
        tft->fillRoundRect(20, y, w - 40, rowH - 4, 6, kBtn);
        tft->drawRoundRect(20, y, w - 40, rowH - 4, 6, kWhite);
        tft->setTextColor(kWhite, kBtn);
        tft->setCursor(28, y + 10);
        tft->print(VerificationSteps_getTestName((VerifyTestId)i));
        y += rowH;
      }
      tft->fillRoundRect(20, h - 44, 80, 36, 6, kBtn);
      tft->drawRoundRect(20, h - 44, 80, 36, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(40, h - 32);
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
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f", (double)s_resultValue);
        tft->setCursor(20, 118);
        tft->print("Value: ");
        tft->print(buf);
        tft->print(" ");
        tft->print(step.unit ? step.unit : "");
        int bx = 20, by = 148, bw = 64, bh = 32, gap = 8;
        float presets[6] = { 0 };
        int nPresets = 0;
        if (step.resultKind == RESULT_CONTINUITY_OHM || step.resultKind == RESULT_EFLI_OHM) {
          presets[0]=0.2f; presets[1]=0.35f; presets[2]=0.5f; presets[3]=1.0f; nPresets=4;
        } else if (step.resultKind == RESULT_IR_MOHM || step.resultKind == RESULT_IR_MOHM_SHEATHED) {
          presets[0]=0.01f; presets[1]=0.5f; presets[2]=1.0f; presets[3]=5.0f; presets[4]=10.0f; nPresets=5;
        } else if (step.resultKind == RESULT_RCD_MS) {
          presets[0]=10.0f; presets[1]=20.0f; presets[2]=30.0f; presets[3]=40.0f; nPresets=4;
        }
        for (int i = 0; i < nPresets && i < 5; i++) {
          int kx = bx + i * (bw + gap);
          tft->fillRoundRect(kx, by, bw, bh, 6, kBtn);
          tft->drawRoundRect(kx, by, bw, bh, 6, kWhite);
          tft->setTextColor(kWhite, kBtn);
          tft->setCursor(kx + 4, by + 8);
          if (step.resultKind == RESULT_RCD_MS) snprintf(buf, sizeof(buf), "%.0f", (double)presets[i]);
          else snprintf(buf, sizeof(buf), "%.2f", (double)presets[i]);
          tft->print(buf);
        }
        tft->fillRoundRect(20, by + bh + 12, w - 40, 44, 8, kGreen);
        tft->drawRoundRect(20, by + bh + 12, w - 40, 44, 8, kWhite);
        tft->setTextColor(TFT_BLACK, kGreen);
        tft->setCursor(w/2 - 28, by + bh + 22);
        tft->print("Confirm");
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
        else if (i == 5) label = field ? "Email settings" : "Email settings (PIN)";
        else if (i == 6) label = field ? "Change operating mode" : "Change operating mode (PIN)";
        else label = "Change PIN (PIN)";
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
      AppState_getTrainingSyncEndpoint(endpoint, sizeof(endpoint));
      AppState_getTrainingSyncCubicleId(cubicle, sizeof(cubicle));
      GoogleSync_getDeviceId(deviceId, sizeof(deviceId));
      GoogleSync_getLastStatus(status, sizeof(status));

      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 34);
      tft->print("Enabled:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(76, 34);
      tft->print(AppState_getTrainingSyncEnabled() ? "On" : "Off");
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 48);
      tft->print("Endpoint:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(82, 48);
      tft->print(endpoint[0] ? "Configured" : "Not set");
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 62);
      tft->print("Cubicle:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(74, 62);
      tft->print(cubicle[0] ? cubicle : "(not set)");
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 76);
      tft->print("Device:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(68, 76);
      tft->print(deviceId);
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 90);
      tft->print("Status:");
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(20, 104);
      tft->print(status);

      int y = 122, btnH = 26, gap = 4;
      tft->fillRoundRect(20, y, w - 40, btnH, 6, kBtn);
      tft->drawRoundRect(20, y, w - 40, btnH, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(28, y + 6);
      tft->print("Toggle sync on/off");
      y += btnH + gap;

      tft->fillRoundRect(20, y, w - 40, btnH, 6, kBtn);
      tft->drawRoundRect(20, y, w - 40, btnH, 6, kWhite);
      tft->setCursor(28, y + 6);
      tft->print("Edit sync endpoint URL");
      y += btnH + gap;

      tft->fillRoundRect(20, y, w - 40, btnH, 6, kBtn);
      tft->drawRoundRect(20, y, w - 40, btnH, 6, kWhite);
      tft->setCursor(28, y + 6);
      tft->print("Edit auth token (optional)");
      y += btnH + gap;

      tft->fillRoundRect(20, y, w - 40, btnH, 6, kBtn);
      tft->drawRoundRect(20, y, w - 40, btnH, 6, kWhite);
      tft->setCursor(28, y + 6);
      tft->print("Edit cubicle ID (e.g. CUB-03)");
      y += btnH + gap;

      tft->fillRoundRect(20, y, w - 40, btnH, 6, kGreen);
      tft->drawRoundRect(20, y, w - 40, btnH, 6, kWhite);
      tft->setTextColor(TFT_BLACK, kGreen);
      tft->setCursor(28, y + 6);
      tft->print("Send test ping");
      y += btnH + gap;

      tft->fillRoundRect(20, y, 80, 26, 6, kBtn);
      tft->drawRoundRect(20, y, 80, 26, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setCursor(36, y + 6);
      tft->print("Back");
      break;
    }
    case SCREEN_TRAINING_SYNC_EDIT: {
      const char* titles[] = { "Sync endpoint URL", "Auth token", "Cubicle ID" };
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
      tft->print("5 digits (authorised users only):");
      char disp[6] = "*****";
      for (int i = 0; i < s_pinLen && i < 5; i++) disp[i] = s_pinDigits[i];
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
      tft->print("Enter 5 digits");
      char disp[6] = "*****";
      for (int i = 0; i < s_pinLen && i < 5; i++) disp[i] = s_pinDigits[i];
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
}

ScreenId Screens_handleTouch(TFT_eSPI* tft, ScreenId current, uint16_t x, uint16_t y) {
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
        return handled(SCREEN_SETTINGS);  /* Return to Settings, not main menu */
      }
      break;
    case SCREEN_MAIN_MENU:
      if (inRect(ix, iy, 20, 76, w - 40, 44)) return handled(SCREEN_TEST_SELECT);
      if (inRect(ix, iy, 20, 132, w - 40, 44)) return handled(SCREEN_REPORT_LIST);
      if (inRect(ix, iy, 20, 188, w - 40, 44)) return handled(SCREEN_SETTINGS);
      break;
    case SCREEN_TEST_SELECT: {
      int rowH = 36, y0 = 56;
      for (int i = 0; i < VERIFY_TEST_COUNT; i++) {
        if (inRect(ix, iy, 20, y0 + i * rowH, w - 40, rowH - 4)) {
          s_selectedTestType = i;
          s_stepIndex = 0;
          s_flowPhase = 0;
          s_resultValue = 0.0f;
          s_resultIsSheathedHeating = false;
          s_resultLabel = NULL;
          s_resultUnit = NULL;
          s_resultClause = NULL;
          syncTrainingFlowEvent("test_started", false, nullptr);
          return handled(SCREEN_TEST_FLOW);
        }
      }
      if (inRect(ix, iy, 20, h - 44, 80, 36)) return handled(SCREEN_MAIN_MENU);
      break;
    }
    case SCREEN_TEST_FLOW: {
      int count = VerificationSteps_getStepCount((VerifyTestId)s_selectedTestType);
      if (inRect(ix, iy, w - 70, 6, 50, 24)) {
        if (s_flowPhase == 1) {
          s_flowPhase = 0;
          s_stepIndex = (count > 0) ? count - 1 : 0;
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
        return handled(SCREEN_TEST_SELECT);
      }
      if (s_flowPhase == 1) {
        if (inRect(ix, iy, 20, h - 52, w - 40, 44)) {
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
            syncTrainingFlowEvent("result_confirmed", true, nullptr);
          } else {
            syncTrainingFlowEvent("step_next", false, nullptr);
          }
          Screens_draw(tft, current);
          return handled(current);
        }
      } else if (step.type == STEP_RESULT_ENTRY) {
        int bx = 20, by = 148, bw = 64, bh = 32, gap = 8;
        float presets[5];
        int nPresets = 0;
        if (step.resultKind == RESULT_CONTINUITY_OHM || step.resultKind == RESULT_EFLI_OHM) {
          presets[0]=0.2f; presets[1]=0.35f; presets[2]=0.5f; presets[3]=1.0f; nPresets=4;
        } else if (step.resultKind == RESULT_IR_MOHM || step.resultKind == RESULT_IR_MOHM_SHEATHED) {
          presets[0]=0.01f; presets[1]=0.5f; presets[2]=1.0f; presets[3]=5.0f; presets[4]=10.0f; nPresets=5;
        } else if (step.resultKind == RESULT_RCD_MS) {
          presets[0]=10.0f; presets[1]=20.0f; presets[2]=30.0f; presets[3]=40.0f; nPresets=4;
        }
        for (int i = 0; i < nPresets && i < 5; i++) {
          int kx = bx + i * (bw + gap);
          if (inRect(ix, iy, kx, by, bw, bh)) {
            s_resultValue = presets[i];
            Screens_draw(tft, current);
            return handled(current);
          }
        }
        if (inRect(ix, iy, 20, by + bh + 12, w - 40, 44)) {
          VerifyResultKind kind = step.resultKind;
          if (kind == RESULT_IR_MOHM && s_resultIsSheathedHeating) kind = RESULT_IR_MOHM_SHEATHED;
          s_resultPass = VerificationSteps_validateResult(kind, s_resultValue, s_resultIsSheathedHeating);
          s_resultLabel = step.resultLabel;
          s_resultUnit = step.unit;
          s_resultClause = VerificationSteps_getClauseForResult(step.resultKind);
          s_flowPhase = 1;
          syncTrainingFlowEvent("result_confirmed", true, nullptr);
          if (AppState_getBuzzerEnabled()) { if (s_resultPass) Buzzer_beepPass(); else Buzzer_beepFail(); }
          Screens_draw(tft, current);
          return handled(current);
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
      int btnH = 30, gap = 2, y = 42;
      bool field = AppState_isFieldMode();
      int nRows = field ? 7 : 8;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_ROTATION);
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_WIFI_LIST);
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) {
        AppState_setBuzzerEnabled(!AppState_getBuzzerEnabled());
        Screens_draw(tft, current);
        return handled(current);
      }
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_ABOUT);
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_UPDATES);
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) {
        if (field) return handled(SCREEN_EMAIL_SETTINGS);
        s_pinLen = 0; s_pinDigits[0] = '\0';
        Screens_setPinSuccessTarget(SCREEN_EMAIL_SETTINGS);
        return handled(SCREEN_PIN_ENTER);
      }
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) {
        if (field) { Screens_setModeSelectChoice(AppState_getMode() == APP_MODE_FIELD ? 1 : 0); return handled(SCREEN_MODE_SELECT); }
        s_pinLen = 0; s_pinDigits[0] = '\0';
        Screens_setPinSuccessTarget(SCREEN_MODE_SELECT);
        return handled(SCREEN_PIN_ENTER);
      }
      y += btnH + gap;
      if (!field && inRect(ix, iy, 20, y, w - 40, btnH)) {
        s_pinLen = 0; s_pinDigits[0] = '\0';
        Screens_setPinSuccessTarget(SCREEN_CHANGE_PIN);
        return handled(SCREEN_PIN_ENTER);
      }
      { int backY = 42 + nRows * (btnH + gap);
        if (inRect(ix, iy, 20, backY, 80, 28)) return handled(SCREEN_MAIN_MENU); }
      break;
    }
    case SCREEN_ROTATION:
      if (inRect(ix, iy, 20, 60, w - 40, 48)) { AppState_setRotation(0); return handled(SCREEN_SETTINGS); }
      if (inRect(ix, iy, 20, 118, w - 40, 48)) { AppState_setRotation(1); return handled(SCREEN_SETTINGS); }
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
        if (WifiManager_connect(s_selectedSsid, s_wifiPassLen > 0 ? s_wifiPass : ""))
          Buzzer_beepPass();
        else
          Buzzer_beepFail();
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
        AppState_setOtaAutoCheckEnabled(!AppState_getOtaAutoCheckEnabled());
        Screens_draw(tft, current);
        return handled(current);
      }
      if (inRect(ix, iy, 30 + half, btnY, half, btnH)) {
        AppState_setOtaAutoInstallEnabled(!AppState_getOtaAutoInstallEnabled());
        Screens_draw(tft, current);
        return handled(current);
      }
      btnY += btnH + gap;
      if (!AppState_isFieldMode() && inRect(ix, iy, 20, btnY, w - 40, btnH)) {
        s_pinLen = 0; s_pinDigits[0] = '\0';
        Screens_setPinSuccessTarget(SCREEN_TRAINING_SYNC);
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
      int y = 122, btnH = 26, gap = 4;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) {
        AppState_setTrainingSyncEnabled(!AppState_getTrainingSyncEnabled());
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
      if (inRect(ix, iy, 20, y, w - 40, btnH)) {
        if (GoogleSync_sendPing()) Buzzer_beepPass();
        else Buzzer_beepFail();
        Screens_draw(tft, current);
        return handled(current);
      }
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, 80, 26)) return handled(SCREEN_UPDATES);
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
        if (s_syncEditField == 0) AppState_setTrainingSyncEndpoint(s_syncEditBuffer);
        else if (s_syncEditField == 1) AppState_setTrainingSyncToken(s_syncEditBuffer);
        else AppState_setTrainingSyncCubicleId(s_syncEditBuffer);
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
        if (s_editingEmailField == 0) AppState_setSmtpServer(s_editBuffer);
        else if (s_editingEmailField == 1) AppState_setSmtpPort(s_editBuffer);
        else if (s_editingEmailField == 2) AppState_setSmtpUser(s_editBuffer);
        else if (s_editingEmailField == 3) AppState_setSmtpPass(s_editBuffer);
        else AppState_setReportToEmail(s_editBuffer);
        return handled(SCREEN_EMAIL_SETTINGS);
      }
      break;
    }
    case SCREEN_PIN_ENTER: {
      if (inRect(ix, iy, w - 70, 8, 50, 28)) {
        s_pinLen = 0;
        s_pinDigits[0] = '\0';
        return handled(SCREEN_SETTINGS);
      }
      int cellW = (w - 50) / 3, cellH = 38, startY = 100, gap = 6;
      const char* keys = "123456789C0E";
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
          int idx = row * 3 + col;
          int kx = 20 + col * (cellW + gap), ky = startY + row * (cellH + gap);
          if (!inRect(ix, iy, kx, ky, cellW, cellH)) continue;
          if (keys[idx] == 'C') {
            s_pinLen = 0;
            s_pinDigits[0] = '\0';
            Screens_draw(tft, current);
            return handled(current);
          }
          if (keys[idx] == 'E') {
            if (s_pinLen != 5) break;
            uint32_t pin = 0;
            for (int i = 0; i < 5; i++)
              pin = pin * 10 + (s_pinDigits[i] - '0');
            if (AppState_checkPin(pin)) {
              s_pinLen = 0;
              s_pinDigits[0] = '\0';
              if (s_pinSuccessTarget == SCREEN_MODE_SELECT)
                Screens_setModeSelectChoice(AppState_getMode() == APP_MODE_FIELD ? 1 : 0);
              return handled(s_pinSuccessTarget);
            } else {
              Buzzer_beepFail();
              s_pinLen = 0;
              s_pinDigits[0] = '\0';
              Screens_draw(tft, current);
              return handled(current);
            }
          }
          if (keys[idx] >= '0' && keys[idx] <= '9' && s_pinLen < 5) {
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
            if (s_pinLen != 5) break;
            if (s_changePinStep == 0) {
              memcpy(s_newPinBuf, s_pinDigits, 6);
              s_newPinLen = 5;
              s_changePinStep = 1;
              s_pinLen = 0; s_pinDigits[0] = '\0';
              Screens_draw(tft, current);
              return handled(current);
            } else {
              bool match = (s_pinLen == 5 && strncmp(s_pinDigits, s_newPinBuf, 5) == 0);
              if (match) {
                uint32_t pin = 0;
                for (int i = 0; i < 5; i++) pin = pin * 10 + (s_newPinBuf[i] - '0');
                AppState_setPin(pin);
                Buzzer_beepPass();
              } else Buzzer_beepFail();
              s_changePinStep = 0; s_pinLen = 0; s_pinDigits[0] = '\0'; s_newPinBuf[0] = '\0'; s_newPinLen = 0;
              return handled(SCREEN_SETTINGS);
            }
          }
          if (keys[idx] >= '0' && keys[idx] <= '9' && s_pinLen < 5) {
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
