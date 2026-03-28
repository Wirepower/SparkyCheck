#include "Screens.h"
#include "AppState.h"
#include "ReportGenerator.h"
#include "TestLimits.h"
#include "Buzzer.h"
#include "WifiManager.h"
#include "AdminPortal.h"
#include "BatteryStatus.h"
#include "OtaUpdate.h"
#include "GoogleSync.h"
#include "EmailTest.h"
#include "Standards.h"
#include "VerificationSteps.h"
#include "SparkyDisplay.h"
#include <qrcode.h>
#include <Arduino.h>
#include <WiFi.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const uint16_t kBg = 0x18E3;
/* Unified neutral button color for all UI buttons. */
static const uint16_t kBtn = 0x2108;
static const uint16_t kAccent = 0xFD20;
static const uint16_t kWhite = 0xFFFF;
/* Use one consistent button color across UI actions. */
static const uint16_t kGreen = kBtn;
static const uint16_t kRed = 0xF800;

/** Readable text on 4.3" landscape or tall portrait (matches EEZ layout helpers). */
static bool sparkyReadableUi(int w, int h) {
  return (w >= 700) || (h >= 700) || (w >= 480 && h >= 600);
}

/** Narrow width (e.g. portrait 480×800): title must sit below Back, not on the same row. */
static bool sparkyTestFlowNarrowHeader(int w) {
  return w < 600;
}

static void sparkyMainMenuLayout(int w, int h, int* y0, int* btnH, int* gap) {
  if (w >= 700) {
    *y0 = 88;
    *btnH = 64;
    *gap = 14;
  } else if (h >= 700) {
    *y0 = 100;
    *btnH = 56;
    *gap = 14;
  } else {
    *y0 = 80;
    *btnH = 48;
    *gap = 12;
  }
}

/** Test list row Y and height (must match EezMockupUi testSelectRowGeometryDims). */
static void sparkyTestSelectRowMetrics(int w, int h, int row, int* outY, int* outRowH) {
  const int n = VerificationSteps_getActiveTestCount();
  const int g = 10;
  const int top = (w >= 700) ? 114 : ((h >= 700) ? 106 : 90);
  int usable = h - top - 28;
  if (usable < n * 28) usable = n * 28;
  int rowH = (usable - (n - 1) * g) / n;
  if (rowH < 28) rowH = 28;
  if (rowH > 54) rowH = 54;
  *outY = top + row * (rowH + g);
  *outRowH = rowH;
}

static int sparkyTestSelectHitIndex(int w, int h, int yScreen, int scrollPx) {
  const int testCount = VerificationSteps_getActiveTestCount();
  for (int i = 0; i < testCount; i++) {
    int y0 = 0, rh = 0;
    sparkyTestSelectRowMetrics(w, h, i, &y0, &rh);
    y0 += scrollPx;
    if (yScreen >= y0 && yScreen < (y0 + rh - 2)) return i;
  }
  return -1;
}

static void sparkyClampTestSelectScroll(int w, int h, int* ioScrollPx) {
  if (!ioScrollPx) return;
  const int testCount = VerificationSteps_getActiveTestCount();
  if (testCount <= 0) {
    *ioScrollPx = 0;
    return;
  }
  int firstY = 0, firstH = 0;
  int lastY = 0, lastH = 0;
  sparkyTestSelectRowMetrics(w, h, 0, &firstY, &firstH);
  sparkyTestSelectRowMetrics(w, h, testCount - 1, &lastY, &lastH);
  const int contentTop = firstY;
  const int contentBottom = lastY + (lastH - 2);
  const int contentH = contentBottom - contentTop;
  const int viewTop = contentTop;
  const int viewBottom = h - 28;
  const int viewH = viewBottom - viewTop;
  const int maxScroll = (contentH > viewH) ? (contentH - viewH) : 0;
  if (*ioScrollPx > 0) *ioScrollPx = 0;
  if (*ioScrollPx < -maxScroll) *ioScrollPx = -maxScroll;
}

/** Word-wrap with each line centered (Adafruit fixed width font). Returns Y after last line. */
static int sparkyDrawWrappedWordsCentered(SparkyTft* tft, const char* text, int w, int y, int margin, uint8_t ts,
                                         uint16_t fg, uint16_t bg) {
  if (!text || !text[0]) return y;
  tft->setTextWrap(false);
  tft->setTextSize(ts);
  tft->setTextColor(fg, bg);
  const int cw = 6 * (int)ts;
  const int lineH = 7 * (int)ts + 4;
  const int maxW = w - 2 * margin;
  if (maxW < cw * 4) return y;

  char lineBuf[140];
  lineBuf[0] = '\0';
  const char* p = text;

  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    char word[72];
    int wl = 0;
    while (*p && *p != ' ' && wl < (int)sizeof(word) - 1) word[wl++] = *p++;
    word[wl] = '\0';
    if (wl <= 0) continue;

    int cur = (int)strlen(lineBuf);
    int need = cur + (cur > 0 ? 1 : 0) + wl;
    if (cur > 0 && need * cw > maxW) {
      int tw = cur * cw;
      int tx = margin + (maxW - tw) / 2;
      if (tx < margin) tx = margin;
      tft->setCursor(tx, y);
      tft->print(lineBuf);
      y += lineH;
      lineBuf[0] = '\0';
      cur = 0;
    }

    if (wl * cw > maxW) {
      for (int i = 0; i < wl; i++) {
        char one[2] = {word[i], '\0'};
        cur = (int)strlen(lineBuf);
        if (cur > 0 && (cur + 1) * cw > maxW) {
          int tw = cur * cw;
          int tx = margin + (maxW - tw) / 2;
          if (tx < margin) tx = margin;
          tft->setCursor(tx, y);
          tft->print(lineBuf);
          y += lineH;
          lineBuf[0] = '\0';
        }
        strncat(lineBuf, one, sizeof(lineBuf) - strlen(lineBuf) - 1);
      }
      continue;
    }

    cur = (int)strlen(lineBuf);
    if (cur + (cur > 0 ? 1 : 0) + wl >= (int)sizeof(lineBuf) - 1) {
      int tw = cur * cw;
      int tx = margin + (maxW - tw) / 2;
      if (tx < margin) tx = margin;
      tft->setCursor(tx, y);
      tft->print(lineBuf);
      y += lineH;
      lineBuf[0] = '\0';
    }
    if (lineBuf[0]) strncat(lineBuf, " ", sizeof(lineBuf) - strlen(lineBuf) - 1);
    strncat(lineBuf, word, sizeof(lineBuf) - strlen(lineBuf) - 1);
  }
  if (lineBuf[0]) {
    int cur = (int)strlen(lineBuf);
    int tw = cur * cw;
    int tx = margin + (maxW - tw) / 2;
    if (tx < margin) tx = margin;
    tft->setCursor(tx, y);
    tft->print(lineBuf);
    y += lineH;
  }
  return y;
}

/** AS/NZS scope under Select test: split at natural break so lines stay balanced and centered. */
static void sparkyDrawVerificationScope(SparkyTft* tft, int w, int y0, uint8_t ts) {
  char scope[96];
  Standards_getVerificationScopeLine(scope, sizeof(scope));
  const char* key = " Sec 8 & ";
  char* hit = strstr(scope, key);
  tft->setTextSize(ts);
  tft->setTextColor(kAccent, kBg);
  if (hit) {
    *hit = '\0';
    const char* b = hit + strlen(key);
    const char* a = scope;
    int tw1 = (int)strlen(a) * 6 * (int)ts;
    int tw2 = (int)strlen(b) * 6 * (int)ts;
    tft->setCursor((w - tw1) / 2, y0);
    tft->print(a);
    tft->setCursor((w - tw2) / 2, y0 + 7 * (int)ts + 2);
    tft->print(b);
  } else {
    sparkyDrawWrappedWordsCentered(tft, scope, w, y0, 20, ts, kAccent, kBg);
  }
}

/** Draw centered label in a button; reduces text size if needed. */
static void sparkyDrawBtnLabel(SparkyTft* tft, int x, int y, int bw, int bh, const char* text, uint8_t ts) {
  if (!text || !text[0]) return;
  tft->setTextWrap(false);
  /* Keep all device button labels consistent and readable. */
  tft->setTextColor(kWhite);
  uint8_t t = ts < 1 ? 1 : ts;
  while (t > 1 && (int)strlen(text) * 6 * (int)t > bw - 12) t--;
  int tw = (int)strlen(text) * 6 * (int)t;
  int tx = x + (bw - tw) / 2;
  if (tx < x + 4) tx = x + 4;
  tft->setTextSize(t);
  const int fh = 7 * (int)t;
  int ty = y + (bh - fh) / 2;
  if (ty < y + 2) ty = y + 2;
  tft->setCursor(tx, ty);
  tft->print(text);
}

/* Device-only friendly expansion for abbreviated test names. */
static const char* sparkyDeviceTestLabel(const char* name) {
  static char buf[96];
  if (!name) return "";
  const char* needle = "SWP D/R";
  const char* hit = strstr(name, needle);
  if (!hit) return name;
  size_t pre = (size_t)(hit - name);
  const char* repl = "SWP Disconnect/Reconnect";
  snprintf(buf, sizeof(buf), "%.*s%s%s", (int)pre, name, repl, hit + strlen(needle));
  return buf;
}

static void sparkyBackLabelCoords(int backW, int backH, int backX, int backY, int* outTx, int* outTy) {
  const char* b = "Back";
  const uint8_t ts = 2;
  int tw = (int)strlen(b) * 6 * (int)ts;
  *outTx = backX + (backW - tw) / 2;
  if (*outTx < backX + 4) *outTx = backX + 4;
  const int fh = 7 * (int)ts;
  *outTy = backY + (backH - fh) / 2;
  if (*outTy < backY + 2) *outTy = backY + 2;
}

static void sparkySettingsLayout(int w, int h, bool field, int* y0, int* btnH, int* gap, int* nRows, int* backY) {
  const bool r = sparkyReadableUi(w, h);
  *gap = 6;
  *btnH = r ? 38 : 32;
  *y0 = 56;
  /* Field: 8 rows (incl. Restart). Training: 10 rows (+ Training sync + Change PIN). */
  *nRows = field ? 8 : 10;
  *backY = *y0 + *nRows * (*btnH + *gap) + 8;
  /* Training has extra rows; tighten vertical rhythm on short panels so Back stays on-screen. */
  if (!field && *nRows >= 10 && h <= 500) {
    *gap = 4;
    if (r && *btnH > 32) *btnH = 34;
    *backY = *y0 + *nRows * (*btnH + *gap) + 8;
  }
}

/** Layout for test-flow numeric entry: shared by draw + touch (STEP_RESULT_ENTRY). */
typedef struct {
  int rcdLineY;   /* >= 0 if "Required max" line is shown */
  int valueRowY;
  int delX, delY, delW, delH;
  int uy, uh, uw, uGap;
  int kx, ky, cellW, cellH, gap;
  uint8_t keypadTs;
} ResultEntryLayout;

static void layoutResultEntry(int w, int h, int yAfterInstr, bool showRcdReq, int unitCount, ResultEntryLayout* L) {
  const int marginX = 20;
  const int bottomPad = 12;
  /* Narrow portrait: allow value/keypad block slightly higher to reclaim vertical space. */
  const int minTop = (w < 600) ? 72 : 84;
  int y = yAfterInstr + 8;
  if (y < minTop) y = minTop;

  if (showRcdReq) {
    L->rcdLineY = y;
    y += 26;
  } else {
    L->rcdLineY = -1;
  }

  const int valueRowH = 40;
  L->valueRowY = y;
  L->delW = 124;
  L->delH = 52;
  L->delX = w - marginX - L->delW;
  L->delY = y + (valueRowH - L->delH) / 2;

  L->uGap = 8;
  L->uh = 44;
  L->uy = y + valueRowH + 8;
  L->uw = unitCount > 0 ? (w - 2 * marginX - (unitCount - 1) * L->uGap) / unitCount : 100;

  L->gap = 10;
  L->kx = marginX;
  L->ky = L->uy + L->uh + L->gap;

  int availH = h - bottomPad - L->ky;
  if (availH < 4 * 44 + 3 * L->gap) {
    L->uh = 36;
    L->ky = L->uy + L->uh + 8;
    availH = h - bottomPad - L->ky;
  }
  if (availH < 0) availH = 0;
  L->cellW = (w - 2 * marginX - 2 * L->gap) / 3;
  if (availH > 3 * L->gap) {
    L->cellH = (availH - 3 * L->gap) / 4;
  } else {
    L->cellH = availH > 0 ? availH / 4 : 28;
  }
  if (L->cellH < 28) L->cellH = 28;
  /* Ensure 4 rows + gaps do not extend past the panel. */
  while (4 * L->cellH + 3 * L->gap > availH && L->cellH > 24) L->cellH--;
  /* Tall portrait: use large keypad digits like landscape (w may be only 480). */
  L->keypadTs = (w >= 700 || h >= 700) ? (uint8_t)3 : (uint8_t)2;
}

/** PIN / Change-PIN / Student ID — 3×4 keypad (same geometry as test-flow result entry). */
typedef struct {
  int kx, ky, cellW, cellH, gap;
  uint8_t keypadTs;
  int backX, backY, backW, backH;
} PinKeypadLayout;

/** First row Y for 3×4 keypad; cell size matches layoutResultEntry keypad block. */
static void layoutNumericKeypad3x4(int w, int h, int kyTop, PinKeypadLayout* L) {
  const int marginX = 20;
  const int bottomPad = 12;
  L->gap = 10;
  L->ky = kyTop;
  L->kx = marginX;
  L->cellW = (w - 2 * marginX - 2 * L->gap) / 3;
  int availH = h - bottomPad - L->ky;
  if (availH < 0) availH = 0;
  if (availH > 3 * L->gap) {
    L->cellH = (availH - 3 * L->gap) / 4;
  } else {
    L->cellH = availH > 0 ? availH / 4 : 28;
  }
  if (L->cellH < 28) L->cellH = 28;
  while (4 * L->cellH + 3 * L->gap > availH && L->cellH > 24) L->cellH--;
  L->keypadTs = (w >= 700 || h >= 700) ? (uint8_t)3 : (uint8_t)2;
}

static void layoutPinKeypadGeometry(int w, int h, bool isChangePin, PinKeypadLayout* L) {
  /* Start below title + hint + masked entry (matches draw). */
  layoutNumericKeypad3x4(w, h, isChangePin ? 82 : 84, L);
  if (isChangePin) {
    L->backX = w - 62;
    L->backY = 8;
    L->backW = 48;
    L->backH = 26;
  } else {
    L->backX = w - 70;
    L->backY = 8;
    L->backW = 50;
    L->backH = 28;
  }
}

/** QWERTY-style email / WiFi password / sync URL keyboards — large keys like test-flow entry. */
typedef struct {
  int marginX;
  int keyStartY;
  int cellW, cellH, gap;
  int ctrlY;
  int ctrlRowH;
  uint8_t keyTs;
} QwertyKeyboardLayout;

static void layoutQwertyKeyboard(int w, int h, int keyStartY, int nKeyRows, int ctrlRowH, QwertyKeyboardLayout* L) {
  /* Match result-entry keypad: margin 20, gap 10, bottom pad 12; narrow panels may tighten gap. */
  const int bottomPad = 12;
  L->marginX = 20;
  L->keyStartY = keyStartY;
  L->gap = 10;
  L->ctrlRowH = ctrlRowH;
  int availH = h - keyStartY - ctrlRowH - 10 - bottomPad;
  if (availH < 30) availH = 30;
  const int rowGaps = nKeyRows > 1 ? nKeyRows - 1 : 0;
  L->cellH = (availH - rowGaps * L->gap) / (nKeyRows > 0 ? nKeyRows : 1);
  if (L->cellH < 28) L->cellH = 28;
  if (L->cellH > 54) L->cellH = 54;
  L->cellW = (w - 2 * L->marginX - 9 * L->gap) / 10;
  if (L->cellW < 26) {
    L->gap = 6;
    L->cellW = (w - 2 * L->marginX - 9 * L->gap) / 10;
    L->cellH = (availH - rowGaps * L->gap) / (nKeyRows > 0 ? nKeyRows : 1);
    if (L->cellH < 26) L->cellH = 26;
    if (L->cellW < 24) {
      L->gap = 4;
      L->cellW = (w - 2 * L->marginX - 9 * L->gap) / 10;
      L->cellH = (availH - rowGaps * L->gap) / (nKeyRows > 0 ? nKeyRows : 1);
      if (L->cellH < 24) L->cellH = 24;
    }
  }
  L->ctrlY = keyStartY + nKeyRows * (L->cellH + L->gap) + 6;
  L->keyTs = (w >= 700 || h >= 700) ? (uint8_t)3 : (uint8_t)2;
}

static void drawQwertyCharKey(SparkyTft* tft, int kx, int ky, int cw, int cellH, char keyChar, uint8_t ts) {
  tft->fillRoundRect(kx, ky, cw, cellH, 8, kBtn);
  tft->drawRoundRect(kx, ky, cw, cellH, 8, kWhite);
  char s[2] = { keyChar, '\0' };
  tft->setTextColor(kWhite, kBtn);
  sparkyDrawBtnLabel(tft, kx, ky, cw, cellH, s, ts);
}

static char s_reportSavedBasename[48] = "";
static int s_modeSelectChoice = 0;  /* 0 = Training, 1 = Field */
/** Bottom of instruction text on STEP_RESULT_ENTRY (for layoutResultEntry in draw + touch). */
static int s_resultEntryYAfterInstr = 0;

/* Verification coach state */
static int s_selectedTestType = 0;
static int s_stepIndex = 0;
static int s_testSelectScrollPx = 0;
static bool s_testSelectTouchActive = false;
static int s_testSelectLastY = 0;
static int s_testSelectAccumDy = 0;
static unsigned long s_testSelectLastTouchMs = 0;
static int s_testSelectTapCandidate = -1;
static unsigned long s_testSelectTapCandidateMs = 0;
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
/** Uppercase for QWERTY letter rows (shared: email, Wi‑Fi, training sync). */
static bool s_oskLetterUpper = false;
/** Text keyboards (email / Wi‑Fi / sync edit): true = QWERTY page, false = numbers & symbols. */
static bool s_oskLettersMode = true;

/** SMTP port (email field index 1) is digits-only — 3×4 keypad, no ABC/123 toggle. */
enum { kEmailFieldSmtpPort = 1, kSmtpPortMaxDigits = 5 };
static bool emailFieldNeedsQwertyOsk(int field) { return field != kEmailFieldSmtpPort; }

/* WiFi list / password state */
static WifiNetwork s_networks[WIFI_MAX_SSIDS];
static int s_networkCount = 0;
static char s_selectedSsid[WIFI_SSID_LEN] = "";
static char s_wifiPass[WIFI_PASS_LEN] = "";
static int s_wifiPassLen = 0;
static bool s_wifiPortalAssist = false;
static char s_wifiPortalUrl[160] = "";
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

void Screens_showSavedPrompt(SparkyTft* tft, const char* detail) {
  showSavedPrompt(tft, detail);
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

static void sparkyResetTestSelectGesture(void) {
  s_testSelectTouchActive = false;
  s_testSelectAccumDy = 0;
  s_testSelectTapCandidate = -1;
}

/** Begin verification for test index `rowIndex`; clears list gesture state. */
static ScreenId sparkyCommitTestSelectRow(int rowIndex) {
  const int n = VerificationSteps_getActiveTestCount();
  if (rowIndex < 0 || rowIndex >= n) {
    sparkyResetTestSelectGesture();
    return SCREEN_TEST_SELECT;
  }
  s_selectedTestType = rowIndex;
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
  sparkyResetTestSelectGesture();
  if (AppState_getMode() == APP_MODE_TRAINING) {
    s_studentInputLen = 0;
    s_studentInput[0] = '\0';
    return SCREEN_STUDENT_ID;
  }
  return SCREEN_TEST_FLOW;
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
  s_oskLetterUpper = false;
  s_oskLettersMode = true;
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

static void applyEmailFieldSave(SparkyTft* tft) {
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
}

static char applyLetterCase(char c, bool upper) {
  if (!upper) return c;
  if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
  return c;
}

/* Shared OSK rows: letters (4 rows) vs numbers/symbols (4 rows) — fewer rows ⇒ larger keys. */
static const char kOskLettersRow0[] = "qwertyuiop";
static const char kOskLettersRow1[] = "asdfghjkl";
static const char kOskLettersRow2[] = "zxcvbnm";
static const char kOskLettersRow3Email[] = "@.-_";
static const char kOskLettersRow3Sync[] = "@.-_/:?&";
static const char kOskNumsRow0[] = "1234567890";
static const char kOskNumsRow1[] = "@.-_/:?&";
static const char kOskNumsRow2[] = "!#$%&*+=^|";
static const char kOskNumsRow3[] = "()[]{};,~";

static void drawOskRow(SparkyTft* tft, const QwertyKeyboardLayout* qk, int startY, const char* rowChars, int nCols) {
  for (int col = 0; col < nCols; col++) {
    int kx = qk->marginX + col * (qk->cellW + qk->gap);
    drawQwertyCharKey(tft, kx, startY, qk->cellW, qk->cellH, rowChars[col], qk->keyTs);
  }
}

static void drawOskRowSyncLetters(SparkyTft* tft, const QwertyKeyboardLayout* qk, int startY, const char* rowChars, int nCols) {
  for (int col = 0; col < nCols; col++) {
    int kx = qk->marginX + col * (qk->cellW + qk->gap);
    char c = applyLetterCase(rowChars[col], s_oskLetterUpper);
    drawQwertyCharKey(tft, kx, startY, qk->cellW, qk->cellH, c, qk->keyTs);
  }
}

/* Small "connected Wi‑Fi" glyph in the top-right status area. */
static void drawWifiConnectedIcon(SparkyTft* tft) {
  if (!WifiManager_isConnected() && !AdminPortal_isApActive()) return;
  const int cx = getW(tft) - 50;
  const int cy = 16;
  const int radii[3] = { 4, 7, 10 };
  for (int i = 0; i < 3; i++) {
    int r = radii[i];
    tft->drawCircle(cx, cy, r, kGreen);
    /* Keep only the upper arc to resemble Wi‑Fi waves. */
    tft->fillRect(cx - r - 1, cy, 2 * r + 2, r + 2, kBg);
  }
  tft->fillCircle(cx, cy + 4, 2, kGreen);
}

static void drawBatteryStatusIcon(SparkyTft* tft) {
  int w = getW(tft);
  const int x = w - 26;
  const int y = 7;
  const int bw = 18;
  const int bh = 10;
  tft->drawRect(x, y, bw, bh, kWhite);
  tft->fillRect(x + bw, y + 3, 2, 4, kWhite);
  int pct = 0;
  if (!BatteryStatus_getPercent(&pct)) {
    /* Show a visible placeholder even when ADC battery sensing is disabled. */
    tft->drawLine(x + 3, y + 2, x + bw - 4, y + bh - 3, kAccent);
    tft->drawLine(x + 3, y + bh - 3, x + bw - 4, y + 2, kAccent);
    return;
  }
  int fillW = (pct * (bw - 4)) / 100;
  uint16_t fill = pct > 50 ? kGreen : (pct > 20 ? kAccent : kRed);
  tft->fillRect(x + 2, y + 2, fillW, bh - 4, fill);
}

static void drawQrCodeBox(SparkyTft* tft, int x, int y, int size, const char* text) {
  if (!text || !text[0]) return;
  uint8_t qData[qrcode_getBufferSize(5)];
  QRCode q;
  qrcode_initText(&q, qData, 5, ECC_LOW, text);
  int cell = size / q.size;
  if (cell < 1) cell = 1;
  int drawW = q.size * cell;
  int ox = x + (size - drawW) / 2;
  int oy = y + (size - drawW) / 2;
  tft->fillRect(x, y, size, size, kWhite);
  for (int my = 0; my < q.size; my++) {
    for (int mx = 0; mx < q.size; mx++) {
      if (qrcode_getModule(&q, mx, my))
        tft->fillRect(ox + mx * cell, oy + my * cell, cell, cell, TFT_BLACK);
    }
  }
  tft->drawRect(x, y, size, size, kWhite);
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
  if (s_selectedTestType < 0 || s_selectedTestType >= VerificationSteps_getActiveTestCount()) return;

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

static void partialRedrawPinMasked(SparkyTft* tft, int w, int cursorY) {
  tft->fillRect(18, cursorY - 4, w - 36, 26, kBg);
  tft->setTextColor(kWhite, kBg);
  tft->setTextSize(2);
  char disp[kPinMaxLen + 1];
  int dlen = s_pinLen;
  if (dlen > kPinMaxLen) dlen = kPinMaxLen;
  for (int i = 0; i < dlen; i++) disp[i] = '*';
  disp[dlen] = '\0';
  if (dlen == 0) strcpy(disp, "(none)");
  tft->setCursor(20, cursorY);
  tft->print(disp);
}

/** Repaint only the value/preview line(s) for keypad/OSK screens (avoids full fillScreen flicker). */
static bool screens_try_partial_redraw(SparkyTft* tft, ScreenId id, int w, int h) {
  switch (id) {
    case SCREEN_PIN_ENTER:
      partialRedrawPinMasked(tft, w, 62);
      sparkyDisplayFlush(tft);
      return true;
    case SCREEN_CHANGE_PIN:
      partialRedrawPinMasked(tft, w, 58);
      sparkyDisplayFlush(tft);
      return true;
    case SCREEN_STUDENT_ID: {
      tft->fillRect(18, 46, w - 36, 28, kBg);
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 52);
      char norm[APP_STATE_DEVICE_ID_LEN] = "";
      if (normalizeStudentId(s_studentInput, norm, sizeof(norm)))
        tft->print(norm);
      else
        tft->print("S");
      sparkyDisplayFlush(tft);
      return true;
    }
    case SCREEN_EMAIL_FIELD_EDIT: {
      if (!emailFieldNeedsQwertyOsk(s_editingEmailField)) {
        tft->fillRect(18, 34, w - 36, 18, kBg);
        tft->setTextColor(kWhite, kBg);
        tft->setTextSize(1);
        tft->setCursor(20, 38);
        tft->print(s_editLen > 0 ? s_editBuffer : "(tap keys)");
      } else {
        tft->fillRect(18, 24, w - 36, 18, kBg);
        tft->setTextColor(kWhite, kBg);
        tft->setTextSize(1);
        tft->setCursor(20, 28);
        tft->print(s_editingEmailField == 3 ? (s_editLen > 0 ? "********" : "(not set)") : (s_editLen > 0 ? s_editBuffer : "(tap keys)"));
      }
      sparkyDisplayFlush(tft);
      return true;
    }
    case SCREEN_WIFI_PASSWORD: {
      tft->fillRect(18, 42, w - 36, 16, kBg);
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(1);
      tft->setCursor(20, 46);
      tft->print(s_wifiPassLen > 0 ? "********" : "(tap keys below)");
      sparkyDisplayFlush(tft);
      return true;
    }
    case SCREEN_TRAINING_SYNC_EDIT: {
      tft->fillRect(18, 24, w - 36, 18, kBg);
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(1);
      tft->setCursor(20, 28);
      if (s_syncEditField == 1)
        tft->print(s_syncEditLen > 0 ? "********" : "(not set)");
      else
        tft->print(s_syncEditLen > 0 ? s_syncEditBuffer : "(tap keys)");
      sparkyDisplayFlush(tft);
      return true;
    }
    case SCREEN_TEST_FLOW: {
      if (s_flowPhase != 0) return false;
      VerifyStep step;
      int count = VerificationSteps_getStepCount((VerifyTestId)s_selectedTestType);
      if (s_stepIndex < 0 || s_stepIndex >= count) return false;
      VerificationSteps_getStep((VerifyTestId)s_selectedTestType, s_stepIndex, &step);
      if (step.type != STEP_RESULT_ENTRY) return false;
      ensureResultEntryInputState(step.resultKind);
      const ResultUnitOption* units = nullptr;
      int unitCount = 0;
      int defaultIdx = 0;
      getResultUnitOptions(step.resultKind, &units, &unitCount, &defaultIdx);
      if (s_resultInputUnitIdx < 0 || s_resultInputUnitIdx >= unitCount) s_resultInputUnitIdx = defaultIdx;
      const bool showRcdReq = (step.resultKind == RESULT_RCD_MS && s_rcdRequiredMaxMs > 0.0f);
      ResultEntryLayout rel;
      layoutResultEntry(w, h, s_resultEntryYAfterInstr, showRcdReq, unitCount, &rel);
      tft->fillRect(18, rel.valueRowY - 2, w - 36, 48, kBg);
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, rel.valueRowY);
      tft->print("Value:");
      tft->setCursor(20, rel.valueRowY + 22);
      tft->print(s_resultInputLen > 0 ? s_resultInput : "(none)");
      tft->print(" ");
      tft->print(units[s_resultInputUnitIdx].label);
      sparkyDisplayFlush(tft);
      return true;
    }
    default:
      return false;
  }
}

static void screens_draw_impl(SparkyTft* tft, ScreenId id, bool fullClear) {
  int w = getW(tft);
  int h = getH(tft);
  if (!fullClear && screens_try_partial_redraw(tft, id, w, h))
    return;

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
      tft->setTextColor(kWhite, s_modeSelectChoice == 0 ? kGreen : kBtn);
      tft->setCursor(40, y + 18);
      tft->print("Training (apprentice / supervised)");
      y += 70;
      tft->fillRoundRect(20, y, btnW, btnH, 8, s_modeSelectChoice == 1 ? kGreen : kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(kWhite, s_modeSelectChoice == 1 ? kGreen : kBtn);
      tft->setCursor(40, y + 18);
      tft->print("Field (qualified electrician)");
      y += 70;
      tft->fillRoundRect(w/2 - 60, y, 120, 44, 8, kAccent);
      tft->drawRoundRect(w/2 - 60, y, 120, 44, 8, kWhite);
      tft->setTextColor(kWhite, kAccent);
      tft->setCursor(w/2 - 35, y + 12);
      tft->print("Continue");
      break;
    }
    case SCREEN_MAIN_MENU: {
      int y0 = 0, btnH = 0, gap = 0;
      sparkyMainMenuLayout(w, h, &y0, &btnH, &gap);
      const bool readable = sparkyReadableUi(w, h);
      bool panelWide = (w >= 700);
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      {
        const char* title = "SparkyCheck";
        int tw = (int)strlen(title) * 6 * 2;
        tft->setCursor((w - tw) / 2, 16);
        tft->print(title);
      }
      tft->setTextSize(1);
      tft->setTextColor(kAccent, kBg);
      {
        const char* mode = AppState_isFieldMode() ? "Field mode" : "Training mode";
        int twm = (int)strlen(mode) * 6;
        tft->setCursor((w - twm) / 2, 44);
        tft->print(mode);
      }
      int btnW = w - 40, y = y0;
      const uint8_t labelSize = readable ? (uint8_t)2 : (uint8_t)1;
      tft->fillRoundRect(20, y, btnW, btnH, 8, kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(kWhite, kBtn);
      sparkyDrawBtnLabel(tft, 20, y, btnW, btnH, "Start verification", labelSize);
      y += btnH + gap;
      tft->fillRoundRect(20, y, btnW, btnH, 8, kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(kWhite, kBtn);
      sparkyDrawBtnLabel(tft, 20, y, btnW, btnH, "View reports", labelSize);
      y += btnH + gap;
      tft->fillRoundRect(20, y, btnW, btnH, 8, kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(kWhite, kBtn);
      sparkyDrawBtnLabel(tft, 20, y, btnW, btnH, "Settings", labelSize);
      break;
    }
    case SCREEN_TEST_SELECT: {
      bool panelWide = (w >= 700);
      const bool readable = sparkyReadableUi(w, h);
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(panelWide ? 3 : 2);
      {
        const char* t = "Select test";
        int tw = (int)strlen(t) * 6 * (panelWide ? 3 : 2);
        tft->setCursor((w - tw) / 2, 10);
        tft->print(t);
      }
      int backW = panelWide ? 96 : 92;
      int backH = 36;
      int backX = w - backW - 12;
      int backY = panelWide ? 10 : 8;
      tft->fillRoundRect(backX, backY, backW, backH, 6, kBtn);
      tft->drawRoundRect(backX, backY, backW, backH, 6, kWhite);
      tft->setTextSize(2);
      tft->setTextColor(kWhite, kBtn);
      {
        int tx = 0, ty = 0;
        sparkyBackLabelCoords(backW, backH, backX, backY, &tx, &ty);
        tft->setCursor(tx, ty);
        tft->print("Back");
      }
      {
        const int scopeY = panelWide ? 48 : (h >= 700 ? 50 : 42);
        sparkyDrawVerificationScope(tft, w, scopeY, 2);
      }
      const int testCount = VerificationSteps_getActiveTestCount();
      sparkyClampTestSelectScroll(w, h, &s_testSelectScrollPx);
      int listTopY = 0, listRowH = 0;
      sparkyTestSelectRowMetrics(w, h, 0, &listTopY, &listRowH);
      const int listBottomY = h - 28;
      for (int i = 0; i < testCount; i++) {
        int y = 0, rh = 0;
        sparkyTestSelectRowMetrics(w, h, i, &y, &rh);
        y += s_testSelectScrollPx;
        const int bh = rh - 2;
        if (y + bh < listTopY || y > listBottomY) continue;
        tft->fillRoundRect(20, y, w - 40, bh, 6, kBtn);
        tft->drawRoundRect(20, y, w - 40, bh, 6, kWhite);
        tft->setTextColor(kWhite, kBtn);
        {
          uint8_t ts = panelWide ? (uint8_t)2 : (readable ? (uint8_t)2 : (uint8_t)1);
          sparkyDrawBtnLabel(tft, 20, y, w - 40, bh, sparkyDeviceTestLabel(VerificationSteps_getTestName((VerifyTestId)i)), ts);
        }
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

      PinKeypadLayout pk;
      layoutNumericKeypad3x4(w, h, 84, &pk);
      const char* keys = "123456789D0E";  /* D=delete, E=start */
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
          int idx = row * 3 + col;
          int kx = pk.kx + col * (pk.cellW + pk.gap);
          int ky = pk.ky + row * (pk.cellH + pk.gap);
          uint16_t cellBg = kBtn;
          if (keys[idx] == 'D') cellBg = kAccent;
          else if (keys[idx] == 'E') cellBg = kGreen;
          tft->fillRoundRect(kx, ky, pk.cellW, pk.cellH, 8, cellBg);
          tft->drawRoundRect(kx, ky, pk.cellW, pk.cellH, 8, kWhite);
          tft->setTextColor(kWhite, cellBg);
          if (keys[idx] == 'D') {
            sparkyDrawBtnLabel(tft, kx, ky, pk.cellW, pk.cellH, "Del", pk.keypadTs);
          } else if (keys[idx] == 'E') {
            sparkyDrawBtnLabel(tft, kx, ky, pk.cellW, pk.cellH, "Start", pk.keypadTs);
          } else {
            char ds[2] = { keys[idx], '\0' };
            sparkyDrawBtnLabel(tft, kx, ky, pk.cellW, pk.cellH, ds, pk.keypadTs);
          }
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
        const int backW = 96, backH = 36;
        const int backX = w - backW - 12;
        const int backY = 8;
        tft->fillRoundRect(backX, backY, backW, backH, 6, kBtn);
        tft->drawRoundRect(backX, backY, backW, backH, 6, kWhite);
        tft->setTextSize(2);
        tft->setTextColor(kWhite, kBtn);
        {
          int tx = 0, ty = 0;
          sparkyBackLabelCoords(backW, backH, backX, backY, &tx, &ty);
          tft->setCursor(tx, ty);
          tft->print("Back");
        }
        const bool narrowResult = sparkyTestFlowNarrowHeader(w);
        const char* rt = "Result";
        const int twr = (int)strlen(rt) * 6 * 2;
        const int yResultTitle = narrowResult ? (backY + backH + 8) : 16;
        tft->setTextColor(kWhite, kBg);
        tft->setTextSize(2);
        tft->setCursor((w - twr) / 2, yResultTitle);
        tft->print(rt);
        const int passBoxY = narrowResult ? (yResultTitle + 7 * 2 + 12) : 52;
        tft->fillRect(20, passBoxY, w - 40, 72, s_resultPass ? kGreen : kRed);
        tft->setTextColor(kWhite, s_resultPass ? kGreen : kRed);
        tft->setTextSize(3);
        {
          const int passTextY = passBoxY + (72 - 7 * 3) / 2;
          tft->setCursor(w / 2 - 30, passTextY);
        }
        tft->print(s_resultPass ? "PASS" : "FAIL");
        tft->setTextSize(2);
        tft->setTextColor(kWhite, kBg);
        int yRes = passBoxY + 72 + 8;
        {
          char buf[80];
          if (s_resultUnit && s_resultUnit[0])
            snprintf(buf, sizeof(buf), "%s: %.3f %s", s_resultLabel ? s_resultLabel : "", (double)s_resultValue, s_resultUnit);
          else
            snprintf(buf, sizeof(buf), "%s: Verified", s_resultLabel ? s_resultLabel : "Test");
          yRes = sparkyDrawWrappedWordsCentered(tft, buf, w, yRes, 20, 2, kWhite, kBg);
        }
        if (s_selectedTestType == (int)VERIFY_RCD && s_rcdRequiredMaxMs > 0.0f) {
          char crit[64];
          snprintf(crit, sizeof(crit), "Criterion: <= %.3f ms", (double)s_rcdRequiredMaxMs);
          tft->setCursor(20, yRes + 4);
          tft->setTextSize(1);
          tft->setTextColor(kAccent, kBg);
          tft->print(crit);
          yRes += 20;
        }
        if (s_resultClause && s_resultClause[0]) {
          tft->setTextSize(1);
          tft->setTextColor(kAccent, kBg);
          yRes = sparkyDrawWrappedWordsCentered(tft, s_resultClause, w, yRes + 6, 20, 1, kAccent, kBg);
        }
        (void)yRes;
        int btnY = h - 56;
        tft->fillRoundRect(20, btnY, w - 40, 48, 8, kAccent);
        tft->drawRoundRect(20, btnY, w - 40, 48, 8, kWhite);
        tft->setTextColor(kWhite, kAccent);
        sparkyDrawBtnLabel(tft, 20, btnY, w - 40, 48, "End session", 2);
        break;
      }
      int count = VerificationSteps_getStepCount((VerifyTestId)s_selectedTestType);
      if (s_stepIndex >= count) break;
      VerifyStep step;
      VerificationSteps_getStep((VerifyTestId)s_selectedTestType, s_stepIndex, &step);
      const bool r = sparkyReadableUi(w, h);
      const uint8_t titleTs = r ? 3 : 2;
      const int backW = 96, backH = 36;
      const int backX = w - backW - 12;
      const int backY = 8;

      tft->fillRoundRect(backX, backY, backW, backH, 6, kBtn);
      tft->drawRoundRect(backX, backY, backW, backH, 6, kWhite);
      tft->setTextSize(2);
      tft->setTextColor(kWhite, kBtn);
      {
        int tx = 0, ty = 0;
        sparkyBackLabelCoords(backW, backH, backX, backY, &tx, &ty);
        tft->setCursor(tx, ty);
        tft->print("Back");
      }
      const bool narrowHeader = sparkyTestFlowNarrowHeader(w);
      int stepProgY;
      if (narrowHeader) {
        const int titleY0 = backY + backH + 6;
        const int titleEndY = sparkyDrawWrappedWordsCentered(
            tft, step.title, w, titleY0, 20, titleTs, step.type == STEP_SAFETY ? kAccent : kWhite, kBg);
        stepProgY = titleEndY + 8;
      } else {
        tft->setTextSize(titleTs);
        tft->setTextColor(step.type == STEP_SAFETY ? kAccent : kWhite, kBg);
        {
          int tw = (int)strlen(step.title) * 6 * titleTs;
          tft->setCursor((w - tw) / 2, 14);
          tft->print(step.title);
        }
        stepProgY = 48;
      }
      tft->setTextSize(2);
      tft->setTextColor(kAccent, kBg);
      {
        char prog[20];
        snprintf(prog, sizeof(prog), "Step %d of %d", s_stepIndex + 1, count);
        tft->setCursor(20, stepProgY);
        tft->print(prog);
      }

      /* Start clause/instruction below "Step n of m" (text size 2 ≈ 16px tall). */
      const int kStepProgressBottom = stepProgY + 8 * 2 + 12;
      int yBelow = kStepProgressBottom;
      if (step.clause && step.clause[0])
        yBelow = sparkyDrawWrappedWordsCentered(tft, step.clause, w, yBelow, 20, 1, kAccent, kBg) + 8;
      int yAfterInstr = sparkyDrawWrappedWordsCentered(tft, step.instruction, w, yBelow, 20, 2, kWhite, kBg);

      if (step.type == STEP_SAFETY) {
        int btnY = h - 56;
        tft->fillRoundRect(20, btnY, w - 40, 52, 8, kGreen);
        tft->drawRoundRect(20, btnY, w - 40, 52, 8, kWhite);
        tft->setTextColor(kWhite, kGreen);
        sparkyDrawBtnLabel(tft, 20, btnY, w - 40, 52, "OK / I have done this", 2);
      } else if (step.type == STEP_VERIFY_YESNO) {
        int btnY = h - 56, half = (w - 50) / 2;
        bool wide = (w >= 700) || (h >= 700);
        tft->fillRoundRect(20, btnY, half, 52, 8, kGreen);
        tft->drawRoundRect(20, btnY, half, 52, 8, kWhite);
        tft->setTextColor(kWhite, kGreen);
        sparkyDrawBtnLabel(tft, 20, btnY, half, 52, "Yes", wide ? 3 : 2);
        tft->fillRoundRect(30 + half, btnY, half, 52, 8, kBtn);
        tft->drawRoundRect(30 + half, btnY, half, 52, 8, kWhite);
        tft->setTextColor(kWhite, kBtn);
        sparkyDrawBtnLabel(tft, 30 + half, btnY, half, 52, "No", wide ? 3 : 2);
      } else if (step.type == STEP_INFO) {
        int btnY = h - 56;
        tft->fillRoundRect(20, btnY, w - 40, 52, 8, kGreen);
        tft->drawRoundRect(20, btnY, w - 40, 52, 8, kWhite);
        tft->setTextColor(kWhite, kGreen);
        sparkyDrawBtnLabel(tft, 20, btnY, w - 40, 52, "OK", 2);
      } else if (step.type == STEP_RESULT_ENTRY) {
        ensureResultEntryInputState(step.resultKind);
        const ResultUnitOption* units = nullptr;
        int unitCount = 0;
        int defaultIdx = 0;
        getResultUnitOptions(step.resultKind, &units, &unitCount, &defaultIdx);
        if (s_resultInputUnitIdx < 0 || s_resultInputUnitIdx >= unitCount) s_resultInputUnitIdx = defaultIdx;

        const bool showRcdReq = (step.resultKind == RESULT_RCD_MS && s_rcdRequiredMaxMs > 0.0f);
        s_resultEntryYAfterInstr = yAfterInstr;
        ResultEntryLayout rel;
        layoutResultEntry(w, h, yAfterInstr, showRcdReq, unitCount, &rel);

        tft->setTextColor(kWhite, kBg);
        tft->setTextSize(2);
        if (rel.rcdLineY >= 0) {
          char reqBuf[44];
          snprintf(reqBuf, sizeof(reqBuf), "Required max: %.3f ms", (double)s_rcdRequiredMaxMs);
          tft->setCursor(20, rel.rcdLineY);
          tft->print(reqBuf);
        }
        tft->setCursor(20, rel.valueRowY);
        tft->print("Value:");
        tft->setCursor(20, rel.valueRowY + 22);
        tft->print(s_resultInputLen > 0 ? s_resultInput : "(none)");
        tft->print(" ");
        tft->print(units[s_resultInputUnitIdx].label);

        tft->fillRoundRect(rel.delX, rel.delY, rel.delW, rel.delH, 8, kAccent);
        tft->drawRoundRect(rel.delX, rel.delY, rel.delW, rel.delH, 8, kWhite);
        tft->setTextColor(kWhite, kAccent);
        sparkyDrawBtnLabel(tft, rel.delX, rel.delY, rel.delW, rel.delH, "Del", 2);

        for (int i = 0; i < unitCount; i++) {
          int ux = 20 + i * (rel.uw + rel.uGap);
          uint16_t bg = (i == s_resultInputUnitIdx) ? kGreen : kBtn;
          tft->fillRoundRect(ux, rel.uy, rel.uw, rel.uh, 8, bg);
          tft->drawRoundRect(ux, rel.uy, rel.uw, rel.uh, 8, kWhite);
          tft->setTextColor(kWhite, bg);
          sparkyDrawBtnLabel(tft, ux, rel.uy, rel.uw, rel.uh, units[i].label, 2);
        }

        const char* keys[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "OK" };
        for (int row = 0; row < 4; row++) {
          for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            int kx = rel.kx + col * (rel.cellW + rel.gap);
            int ky = rel.ky + row * (rel.cellH + rel.gap);
            uint16_t bg = (strcmp(keys[idx], "OK") == 0) ? kGreen : kBtn;
            tft->fillRoundRect(kx, ky, rel.cellW, rel.cellH, 8, bg);
            tft->drawRoundRect(kx, ky, rel.cellW, rel.cellH, 8, kWhite);
            tft->setTextColor(kWhite, bg);
            sparkyDrawBtnLabel(tft, kx, ky, rel.cellW, rel.cellH, keys[idx], rel.keypadTs);
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
      {
        const char* t = "Settings";
        int tw = (int)strlen(t) * 6 * 2;
        tft->setCursor((w - tw) / 2, 10);
        tft->print(t);
      }
      int y = 0, btnH = 0, gap = 0, nRows = 0, backY = 0;
      bool field = AppState_isFieldMode();
      sparkySettingsLayout(w, h, field, &y, &btnH, &gap, &nRows, &backY);
      const int btnW = w - 40;
      const uint8_t lblTs = sparkyReadableUi(w, h) ? (uint8_t)2 : (uint8_t)1;
      for (int i = 0; i < nRows; i++) {
        const char* label = "";
        if (field) {
          if (i == 0) label = "Screen rotation";
          else if (i == 1) label = "WiFi connection";
          else if (i == 2) label = "Buzzer (sound)";
          else if (i == 3) label = "About";
          else if (i == 4) label = "Firmware updates";
          else if (i == 5) label = "Email settings";
          else if (i == 6) label = "Mode change (boot hold)";
          else label = "Restart device";
        } else {
          if (i == 0) label = "Screen rotation";
          else if (i == 1) label = "WiFi connection";
          else if (i == 2) label = "Buzzer (sound)";
          else if (i == 3) label = "About";
          else if (i == 4) label = "Firmware updates";
          else if (i == 5) label = "Training sync (PIN)";
          else if (i == 6) label = "Email settings";
          else if (i == 7) label = "Mode change (boot hold)";
          else if (i == 8) label = "Restart device";
          else label = "Change PIN";
        }
        tft->fillRoundRect(20, y, btnW, btnH, 6, kBtn);
        tft->drawRoundRect(20, y, btnW, btnH, 6, kWhite);
        tft->setTextColor(kWhite, kBtn);
        if (i == 1) {
          tft->setTextSize(lblTs);
          tft->setCursor(28, y + 4);
          tft->print("WiFi connection");
          tft->setTextSize(1);
          tft->setCursor(28, y + 4 + (lblTs == 2 ? 16 : 14));
          if (WifiManager_isConnected()) {
            char ip[20];
            WifiManager_getIpString(ip, sizeof(ip));
            tft->print(ip);
          } else
            tft->print("Not connected");
        } else if (i == 2) {
          tft->setTextSize(lblTs);
          tft->setCursor(28, y + 4);
          tft->print("Buzzer (sound)");
          tft->setTextSize(1);
          tft->setCursor(28, y + 4 + (lblTs == 2 ? 16 : 14));
          tft->print(AppState_getBuzzerEnabled() ? "On" : "Off");
        } else
          sparkyDrawBtnLabel(tft, 20, y, btnW, btnH, label, lblTs);
        y += btnH + gap;
      }
      {
        const int backH = 40;
        tft->fillRoundRect(20, backY, btnW, backH, 6, kBtn);
        tft->drawRoundRect(20, backY, btnW, backH, 6, kWhite);
        tft->setTextColor(kWhite, kBtn);
        sparkyDrawBtnLabel(tft, 20, backY, btnW, backH, "Back", 2);
      }
      break;
    }
    case SCREEN_EMAIL_SETTINGS: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 8);
      tft->print("Email settings");
      tft->setTextSize(1);
      char tmp[APP_STATE_EMAIL_STR_LEN];
      int rowH = 20, y = 34;
      AppState_getSmtpServer(tmp, sizeof(tmp));
      tft->setCursor(20, y); tft->print("1. SMTP server"); tft->setCursor(w/2, y); tft->print(strlen(tmp) ? tmp : "-"); y += rowH;
      AppState_getSmtpPort(tmp, sizeof(tmp));
      tft->setCursor(20, y); tft->print("2. Port"); tft->setCursor(w/2, y); tft->print(strlen(tmp) ? tmp : "587"); y += rowH;
      AppState_getSmtpUser(tmp, sizeof(tmp));
      tft->setCursor(20, y); tft->print("3. Sender email"); tft->setCursor(w/2, y); tft->print(strlen(tmp) ? tmp : "-"); y += rowH;
      AppState_getSmtpPass(tmp, sizeof(tmp));
      tft->setCursor(20, y); tft->print("4. SMTP password"); tft->setCursor(w/2, y); tft->print(strlen(tmp) ? "****" : "-"); y += rowH;
      AppState_getReportToEmail(tmp, sizeof(tmp));
      tft->setCursor(20, y); tft->print(AppState_isFieldMode() ? "5. Recipient email" : "5. Teacher email"); tft->setCursor(w/2, y); tft->print(strlen(tmp) ? tmp : "-"); y += rowH + 2;
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, y);
      tft->print("Tap any row above to edit.");
      tft->fillRoundRect(20, h - 72, w - 40, 26, 6, kBtn);
      tft->drawRoundRect(20, h - 72, w - 40, 26, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      sparkyDrawBtnLabel(tft, 20, h - 72, w - 40, 26, "Send test email", 1);
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
      if (emailFieldNeedsQwertyOsk(s_editingEmailField)) {
        tft->setCursor(20, 28);
        tft->print(s_editingEmailField == 3 ? (s_editLen > 0 ? "********" : "(not set)") : (s_editLen > 0 ? s_editBuffer : "(tap keys)"));
        QwertyKeyboardLayout qk;
        layoutQwertyKeyboard(w, h, 48, 4, 40, &qk);
        int startY = qk.keyStartY;
        if (s_oskLettersMode) {
          drawOskRowSyncLetters(tft, &qk, startY, kOskLettersRow0, 10);
          startY += qk.cellH + qk.gap;
          drawOskRowSyncLetters(tft, &qk, startY, kOskLettersRow1, 9);
          startY += qk.cellH + qk.gap;
          drawOskRowSyncLetters(tft, &qk, startY, kOskLettersRow2, 7);
          startY += qk.cellH + qk.gap;
          drawOskRow(tft, &qk, startY, kOskLettersRow3Email, 4);
        } else {
          drawOskRow(tft, &qk, startY, kOskNumsRow0, 10);
          startY += qk.cellH + qk.gap;
          drawOskRow(tft, &qk, startY, kOskNumsRow1, 8);
          startY += qk.cellH + qk.gap;
          drawOskRow(tft, &qk, startY, kOskNumsRow2, 10);
          startY += qk.cellH + qk.gap;
          drawOskRow(tft, &qk, startY, kOskNumsRow3, 10);
        }
        {
          const int delW = 52, aaW = 44, modeW = 56;
          int aaX = qk.marginX + delW + qk.gap;
          int modeX = aaX + aaW + qk.gap;
          int saveX = modeX + modeW + qk.gap;
          int saveW = w - saveX - qk.marginX;
          tft->fillRoundRect(qk.marginX, qk.ctrlY, delW, qk.ctrlRowH, 8, kAccent);
          tft->drawRoundRect(qk.marginX, qk.ctrlY, delW, qk.ctrlRowH, 8, kWhite);
          tft->setTextColor(kWhite, kAccent);
          sparkyDrawBtnLabel(tft, qk.marginX, qk.ctrlY, delW, qk.ctrlRowH, "Del", qk.keyTs);
          tft->fillRoundRect(aaX, qk.ctrlY, aaW, qk.ctrlRowH, 8, kBtn);
          tft->drawRoundRect(aaX, qk.ctrlY, aaW, qk.ctrlRowH, 8, kWhite);
          tft->setTextColor(kWhite, kBtn);
          sparkyDrawBtnLabel(tft, aaX, qk.ctrlY, aaW, qk.ctrlRowH, "Aa", qk.keyTs);
          tft->fillRoundRect(modeX, qk.ctrlY, modeW, qk.ctrlRowH, 8, kBtn);
          tft->drawRoundRect(modeX, qk.ctrlY, modeW, qk.ctrlRowH, 8, kWhite);
          tft->setTextColor(kWhite, kBtn);
          sparkyDrawBtnLabel(tft, modeX, qk.ctrlY, modeW, qk.ctrlRowH, s_oskLettersMode ? "123" : "ABC", qk.keyTs);
          tft->fillRoundRect(saveX, qk.ctrlY, saveW, qk.ctrlRowH, 8, kGreen);
          tft->drawRoundRect(saveX, qk.ctrlY, saveW, qk.ctrlRowH, 8, kWhite);
          tft->setTextColor(kWhite, kGreen);
          sparkyDrawBtnLabel(tft, saveX, qk.ctrlY, saveW, qk.ctrlRowH, "Save", qk.keyTs);
        }
      } else {
        /* SMTP port: digits only — same 3×4 geometry as PIN / student ID (no QWERTY or 123 toggle). */
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(20, 22);
        tft->print("TCP port 1-65535 (digits only).");
        tft->setTextColor(kWhite, kBg);
        tft->setCursor(20, 38);
        tft->print(s_editLen > 0 ? s_editBuffer : "(tap keys)");
        PinKeypadLayout pk;
        layoutNumericKeypad3x4(w, h, 84, &pk);
        const char* keys = "123456789D0E"; /* D=Del, E=Save */
        for (int row = 0; row < 4; row++) {
          for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            int kx = pk.kx + col * (pk.cellW + pk.gap);
            int ky = pk.ky + row * (pk.cellH + pk.gap);
            uint16_t cellBg = kBtn;
            if (keys[idx] == 'D') cellBg = kAccent;
            else if (keys[idx] == 'E') cellBg = kGreen;
            tft->fillRoundRect(kx, ky, pk.cellW, pk.cellH, 8, cellBg);
            tft->drawRoundRect(kx, ky, pk.cellW, pk.cellH, 8, kWhite);
            tft->setTextColor(kWhite, cellBg);
            if (keys[idx] == 'D') {
              sparkyDrawBtnLabel(tft, kx, ky, pk.cellW, pk.cellH, "Del", pk.keypadTs);
            } else if (keys[idx] == 'E') {
              sparkyDrawBtnLabel(tft, kx, ky, pk.cellW, pk.cellH, "Save", pk.keypadTs);
            } else {
              char ds[2] = { keys[idx], '\0' };
              sparkyDrawBtnLabel(tft, kx, ky, pk.cellW, pk.cellH, ds, pk.keypadTs);
            }
          }
        }
      }
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
      {
        const char* t = "Screen orientation";
        int tw = (int)strlen(t) * 6 * 2;
        tft->setCursor((w - tw) / 2, 10);
        tft->print(t);
      }
      int r = AppState_getRotation();
      int btnW = w - 40, btnH = 48, y = 60;
      tft->fillRoundRect(20, y, btnW, btnH, 8, r == 0 ? kGreen : kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(kWhite, r == 0 ? kGreen : kBtn);
      sparkyDrawBtnLabel(tft, 20, y, btnW, btnH, "Portrait", 2);
      y += 58;
      tft->fillRoundRect(20, y, btnW, btnH, 8, r == 1 ? kGreen : kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(kWhite, r == 1 ? kGreen : kBtn);
      sparkyDrawBtnLabel(tft, 20, y, btnW, btnH, "Landscape", 2);
      {
        const int backH = 44;
        const int by = h - 52;
        tft->setTextColor(kWhite, kBtn);
        tft->fillRoundRect(20, by, btnW, backH, 6, kBtn);
        tft->drawRoundRect(20, by, btnW, backH, 6, kWhite);
        sparkyDrawBtnLabel(tft, 20, by, btnW, backH, "Back", 2);
      }
      break;
    }
    case SCREEN_WIFI_LIST: {
      char adminIp[20] = "";
      char apSsid[32] = "", apPass[32] = "";
      bool hasAdminIp = WifiManager_getIpString(adminIp, sizeof(adminIp));
      if (!hasAdminIp && AdminPortal_isApActive()) {
        hasAdminIp = AdminPortal_getApIp(adminIp, sizeof(adminIp));
        AdminPortal_getApSsid(apSsid, sizeof(apSsid));
        AdminPortal_getApPass(apPass, sizeof(apPass));
      }
      if (s_wifiPortalAssist) {
        tft->setTextColor(kWhite, kBg);
        tft->setTextSize(2);
        tft->setCursor(20, 8);
        tft->print("Portal Assist");
        tft->setTextSize(1);
        tft->setCursor(20, 34);
        tft->print("Scan with phone to open portal login.");
        tft->setCursor(20, 48);
        tft->print("May fail if venue binds by MAC/session.");
        drawQrCodeBox(tft, 20, 66, 140, s_wifiPortalUrl[0] ? s_wifiPortalUrl : "http://neverssl.com/");
        tft->drawRoundRect(20, 66, 140, 140, 6, kWhite);
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(176, 72);
        tft->print("URL:");
        tft->setTextColor(kWhite, kBg);
        tft->setCursor(176, 88);
        tft->print(s_wifiPortalUrl[0] ? s_wifiPortalUrl : "http://neverssl.com/");
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(176, 108);
        tft->print("Admin URL:");
        tft->setTextColor(kWhite, kBg);
        tft->setCursor(176, 122);
        if (hasAdminIp) {
          tft->print("http://");
          tft->print(adminIp);
          tft->print("/admin");
        } else {
          tft->print("(IP not available yet)");
        }
        tft->fillRoundRect(176, 144, w - 196, 34, 6, kGreen);
        tft->drawRoundRect(176, 144, w - 196, 34, 6, kWhite);
        tft->setTextColor(kWhite, kGreen);
        sparkyDrawBtnLabel(tft, 176, 144, w - 196, 34, "Retry check", 2);
        const int backX = 20, backY = h - 48, backW = 124, backH = 40;
        tft->fillRoundRect(backX, backY, backW, backH, 6, kBtn);
        tft->drawRoundRect(backX, backY, backW, backH, 6, kWhite);
        tft->setTextColor(kWhite, kBtn);
        sparkyDrawBtnLabel(tft, backX, backY, backW, backH, "Back", 2);
        break;
      }
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      {
        const char* t = "WiFi";
        int tw = (int)strlen(t) * 6 * 2;
        tft->setCursor((w - tw) / 2, 8);
        tft->print(t);
      }
      tft->setTextSize(1);
      if (WifiManager_isConnected()) {
        char ssid[WIFI_SSID_LEN], ip[20];
        WifiManager_getConnectedSsid(ssid, sizeof(ssid));
        if (!hasAdminIp) ip[0] = '\0';
        else strncpy(ip, adminIp, sizeof(ip) - 1), ip[sizeof(ip) - 1] = '\0';
        tft->setCursor(20, 34);
        tft->print("Connected: ");
        tft->print(ssid);
        tft->setCursor(20, 48);
        tft->print(ip);
      } else {
        tft->setCursor(20, 34);
        tft->print("Not connected");
      }
      const int scanX = 20, scanY = 62, scanW = 124, scanH = 34, rowH = 38;
      tft->fillRoundRect(scanX, scanY, scanW, scanH, 6, kGreen);
      tft->drawRoundRect(scanX, scanY, scanW, scanH, 6, kWhite);
      tft->setTextColor(kWhite, kGreen);
      sparkyDrawBtnLabel(tft, scanX, scanY, scanW, scanH, "Scan", 2);
      int listY = scanY + scanH + 6;
      for (int i = 0; i < s_networkCount && listY + rowH < h - 44; i++) {
        tft->setTextColor(kWhite, kBg);
        tft->setTextSize(2);
        tft->setCursor(20, listY + 4);
        tft->print(s_networks[i].ssid);
        tft->setTextSize(1);
        tft->setCursor(w - 58, listY + 14);
        tft->print(s_networks[i].rssi);
        tft->print(" dBm");
        listY += rowH;
      }
      tft->setTextSize(1);
      const int backX = 20, backY = h - 48, backW = 124, backH = 40;
      tft->fillRoundRect(backX, backY, backW, backH, 6, kBtn);
      tft->drawRoundRect(backX, backY, backW, backH, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      sparkyDrawBtnLabel(tft, backX, backY, backW, backH, "Back", 2);
      tft->setTextColor(kAccent, kBg);
      tft->setTextSize(2);
      tft->setCursor(152, h - 38);
      if (hasAdminIp) {
        tft->print("Admin: ");
        tft->setTextSize(1);
        tft->setCursor(152, h - 18);
        tft->print("http://");
        tft->print(adminIp);
        tft->print("/admin");
      } else {
        tft->setTextSize(1);
        tft->setCursor(152, h - 24);
        tft->print("Admin URL available");
        tft->setCursor(152, h - 12);
        tft->print("when connected");
      }
      if (apSsid[0]) {
        tft->setTextSize(1);
        tft->setTextColor(kWhite, kBg);
        tft->setCursor(20, h - 66);
        tft->print("Hotspot: ");
        tft->print(apSsid);
        tft->setCursor(20, h - 56);
        tft->print("Pass: ");
        tft->print(apPass);
      }
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
      QwertyKeyboardLayout qk;
      layoutQwertyKeyboard(w, h, 68, 4, 40, &qk);
      int startY = qk.keyStartY;
      if (s_oskLettersMode) {
        drawOskRowSyncLetters(tft, &qk, startY, kOskLettersRow0, 10);
        startY += qk.cellH + qk.gap;
        drawOskRowSyncLetters(tft, &qk, startY, kOskLettersRow1, 9);
        startY += qk.cellH + qk.gap;
        drawOskRowSyncLetters(tft, &qk, startY, kOskLettersRow2, 7);
        startY += qk.cellH + qk.gap;
        drawOskRow(tft, &qk, startY, kOskLettersRow3Email, 4);
      } else {
        drawOskRow(tft, &qk, startY, kOskNumsRow0, 10);
        startY += qk.cellH + qk.gap;
        drawOskRow(tft, &qk, startY, kOskNumsRow1, 8);
        startY += qk.cellH + qk.gap;
        drawOskRow(tft, &qk, startY, kOskNumsRow2, 10);
        startY += qk.cellH + qk.gap;
        drawOskRow(tft, &qk, startY, kOskNumsRow3, 10);
      }
      {
        const int delW = 52, aaW = 44, modeW = 56;
        int aaX = qk.marginX + delW + qk.gap;
        int modeX = aaX + aaW + qk.gap;
        int connX = modeX + modeW + qk.gap;
        int connW = w - connX - qk.marginX;
        tft->fillRoundRect(qk.marginX, qk.ctrlY, delW, qk.ctrlRowH, 8, kAccent);
        tft->drawRoundRect(qk.marginX, qk.ctrlY, delW, qk.ctrlRowH, 8, kWhite);
        tft->setTextColor(kWhite, kAccent);
        sparkyDrawBtnLabel(tft, qk.marginX, qk.ctrlY, delW, qk.ctrlRowH, "Del", qk.keyTs);
        tft->fillRoundRect(aaX, qk.ctrlY, aaW, qk.ctrlRowH, 8, kBtn);
        tft->drawRoundRect(aaX, qk.ctrlY, aaW, qk.ctrlRowH, 8, kWhite);
        tft->setTextColor(kWhite, kBtn);
        sparkyDrawBtnLabel(tft, aaX, qk.ctrlY, aaW, qk.ctrlRowH, "Aa", qk.keyTs);
        tft->fillRoundRect(modeX, qk.ctrlY, modeW, qk.ctrlRowH, 8, kBtn);
        tft->drawRoundRect(modeX, qk.ctrlY, modeW, qk.ctrlRowH, 8, kWhite);
        tft->setTextColor(kWhite, kBtn);
        sparkyDrawBtnLabel(tft, modeX, qk.ctrlY, modeW, qk.ctrlRowH, s_oskLettersMode ? "123" : "ABC", qk.keyTs);
        tft->fillRoundRect(connX, qk.ctrlY, connW, qk.ctrlRowH, 8, kGreen);
        tft->drawRoundRect(connX, qk.ctrlY, connW, qk.ctrlRowH, 8, kWhite);
        tft->setTextColor(kWhite, kGreen);
        sparkyDrawBtnLabel(tft, connX, qk.ctrlY, connW, qk.ctrlRowH, "Connect", qk.keyTs);
      }
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

      int btnY = 150, btnH = 30, gap = 6;
      tft->fillRoundRect(20, btnY, w - 40, btnH, 6, kGreen);
      tft->drawRoundRect(20, btnY, w - 40, btnH, 6, kWhite);
      tft->setTextColor(kWhite, kGreen);
      tft->setCursor(w / 2 - 36, btnY + 8);
      tft->print("Check now");

      btnY += btnH + gap;
      uint16_t installColor = OtaUpdate_hasPendingUpdate() ? kAccent : kBtn;
      uint16_t installText = kWhite;
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
      tft->setTextColor(kWhite, kGreen);
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

      QwertyKeyboardLayout qk;
      layoutQwertyKeyboard(w, h, 48, 4, 40, &qk);
      int startY = qk.keyStartY;
      if (s_oskLettersMode) {
        drawOskRowSyncLetters(tft, &qk, startY, kOskLettersRow0, 10);
        startY += qk.cellH + qk.gap;
        drawOskRowSyncLetters(tft, &qk, startY, kOskLettersRow1, 9);
        startY += qk.cellH + qk.gap;
        drawOskRowSyncLetters(tft, &qk, startY, kOskLettersRow2, 7);
        startY += qk.cellH + qk.gap;
        drawOskRow(tft, &qk, startY, kOskLettersRow3Sync, 8);
      } else {
        drawOskRow(tft, &qk, startY, kOskNumsRow0, 10);
        startY += qk.cellH + qk.gap;
        drawOskRow(tft, &qk, startY, kOskNumsRow1, 8);
        startY += qk.cellH + qk.gap;
        drawOskRow(tft, &qk, startY, kOskNumsRow2, 10);
        startY += qk.cellH + qk.gap;
        drawOskRow(tft, &qk, startY, kOskNumsRow3, 10);
      }

      {
        const int delW = 52, aaW = 44, modeW = 56;
        int aaX = qk.marginX + delW + qk.gap;
        int modeX = aaX + aaW + qk.gap;
        int saveX = modeX + modeW + qk.gap;
        int saveW = w - saveX - qk.marginX;
        tft->fillRoundRect(qk.marginX, qk.ctrlY, delW, qk.ctrlRowH, 8, kAccent);
        tft->drawRoundRect(qk.marginX, qk.ctrlY, delW, qk.ctrlRowH, 8, kWhite);
        tft->setTextColor(kWhite, kAccent);
        sparkyDrawBtnLabel(tft, qk.marginX, qk.ctrlY, delW, qk.ctrlRowH, "Del", qk.keyTs);
        tft->fillRoundRect(aaX, qk.ctrlY, aaW, qk.ctrlRowH, 8, kBtn);
        tft->drawRoundRect(aaX, qk.ctrlY, aaW, qk.ctrlRowH, 8, kWhite);
        tft->setTextColor(kWhite, kBtn);
        sparkyDrawBtnLabel(tft, aaX, qk.ctrlY, aaW, qk.ctrlRowH, "Aa", qk.keyTs);
        tft->fillRoundRect(modeX, qk.ctrlY, modeW, qk.ctrlRowH, 8, kBtn);
        tft->drawRoundRect(modeX, qk.ctrlY, modeW, qk.ctrlRowH, 8, kWhite);
        tft->setTextColor(kWhite, kBtn);
        sparkyDrawBtnLabel(tft, modeX, qk.ctrlY, modeW, qk.ctrlRowH, s_oskLettersMode ? "123" : "ABC", qk.keyTs);
        tft->fillRoundRect(saveX, qk.ctrlY, saveW, qk.ctrlRowH, 8, kGreen);
        tft->drawRoundRect(saveX, qk.ctrlY, saveW, qk.ctrlRowH, 8, kWhite);
        tft->setTextColor(kWhite, kGreen);
        sparkyDrawBtnLabel(tft, saveX, qk.ctrlY, saveW, qk.ctrlRowH, "Save", qk.keyTs);
      }

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
      PinKeypadLayout pk;
      layoutPinKeypadGeometry(w, h, false, &pk);
      const char* keys = "123456789C0E";  /* C=Clear, E=Enter/Confirm */
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
          int idx = row * 3 + col;
          int kx = pk.kx + col * (pk.cellW + pk.gap);
          int ky = pk.ky + row * (pk.cellH + pk.gap);
          uint16_t cellBg = kBtn;
          if (keys[idx] == 'C') cellBg = kAccent;
          else if (keys[idx] == 'E') cellBg = kGreen;
          tft->fillRoundRect(kx, ky, pk.cellW, pk.cellH, 8, cellBg);
          tft->drawRoundRect(kx, ky, pk.cellW, pk.cellH, 8, kWhite);
          tft->setTextColor(kWhite, cellBg);
          if (keys[idx] == 'C') {
            sparkyDrawBtnLabel(tft, kx, ky, pk.cellW, pk.cellH, "Clear", pk.keypadTs);
          } else if (keys[idx] == 'E') {
            sparkyDrawBtnLabel(tft, kx, ky, pk.cellW, pk.cellH, "Confirm", pk.keypadTs);
          } else {
            char ds[2] = { keys[idx], '\0' };
            sparkyDrawBtnLabel(tft, kx, ky, pk.cellW, pk.cellH, ds, pk.keypadTs);
          }
        }
      }
      tft->fillRoundRect(pk.backX, pk.backY, pk.backW, pk.backH, 6, kBtn);
      tft->drawRoundRect(pk.backX, pk.backY, pk.backW, pk.backH, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      sparkyDrawBtnLabel(tft, pk.backX, pk.backY, pk.backW, pk.backH, "Back", 2);
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
      PinKeypadLayout pk;
      layoutPinKeypadGeometry(w, h, true, &pk);
      const char* keys = "123456789C0E";
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
          int idx = row * 3 + col;
          int kx = pk.kx + col * (pk.cellW + pk.gap);
          int ky = pk.ky + row * (pk.cellH + pk.gap);
          uint16_t cellBg = kBtn;
          if (keys[idx] == 'C') cellBg = kAccent;
          else if (keys[idx] == 'E') cellBg = kGreen;
          tft->fillRoundRect(kx, ky, pk.cellW, pk.cellH, 8, cellBg);
          tft->drawRoundRect(kx, ky, pk.cellW, pk.cellH, 8, kWhite);
          tft->setTextColor(kWhite, cellBg);
          if (keys[idx] == 'C') {
            sparkyDrawBtnLabel(tft, kx, ky, pk.cellW, pk.cellH, "Clear", pk.keypadTs);
          } else if (keys[idx] == 'E') {
            sparkyDrawBtnLabel(tft, kx, ky, pk.cellW, pk.cellH, "OK", pk.keypadTs);
          } else {
            char ds[2] = { keys[idx], '\0' };
            sparkyDrawBtnLabel(tft, kx, ky, pk.cellW, pk.cellH, ds, pk.keypadTs);
          }
        }
      }
      tft->fillRoundRect(pk.backX, pk.backY, pk.backW, pk.backH, 6, kBtn);
      tft->drawRoundRect(pk.backX, pk.backY, pk.backW, pk.backH, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      sparkyDrawBtnLabel(tft, pk.backX, pk.backY, pk.backW, pk.backH, "Back", 2);
      break;
    }
    default:
      break;
  }
  drawWifiConnectedIcon(tft);
  drawBatteryStatusIcon(tft);
  sparkyDisplayFlush(tft);
}

extern "C" void Screens_draw(SparkyTft* tft, ScreenId id) {
  screens_draw_impl(tft, id, true);
}

void Screens_draw(SparkyTft* tft, ScreenId id, bool fullClear) {
  screens_draw_impl(tft, id, fullClear);
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
      int y0 = 0, btnH = 0, gap = 0;
      sparkyMainMenuLayout(w, h, &y0, &btnH, &gap);
      int y1 = y0 + btnH + gap;
      int y2 = y1 + btnH + gap;
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
      int backW = panelWide ? 96 : 92;
      int backH = 36;
      int backX = w - backW - 12;
      int backY = panelWide ? 10 : 8;
      if (inRect(ix, iy, backX, backY, backW, backH)) {
        sparkyResetTestSelectGesture();
        return handled(SCREEN_MAIN_MENU);
      }
      unsigned long now = millis();
      if (s_testSelectTouchActive && (long)(now - s_testSelectLastTouchMs) > 300) {
        sparkyResetTestSelectGesture();
      }
      const int testCount = VerificationSteps_getActiveTestCount();
      if (testCount <= 0) break;
      int listTopY = 0, listRowH = 0;
      sparkyTestSelectRowMetrics(w, h, 0, &listTopY, &listRowH);
      int listBottomY = h - 28;
      if (inRect(ix, iy, 20, listTopY, w - 40, listBottomY - listTopY)) {
        /*
         * main.cpp only calls handleTouch on touch-down edge. Drag + release are handled by
         * Screens_handleTouchDrag / Screens_handleTouchEnd so a tap completes on finger-up.
         * Always (re)start the gesture on each down edge so a missed touch-up cannot strand state.
         */
        s_testSelectTouchActive = true;
        s_testSelectLastY = iy;
        s_testSelectAccumDy = 0;
        s_testSelectLastTouchMs = now;
        s_testSelectTapCandidate = sparkyTestSelectHitIndex(w, h, iy, s_testSelectScrollPx);
        s_testSelectTapCandidateMs = now;
        return handled(current);
      }
      break;
    }
    case SCREEN_STUDENT_ID: {
      if (inRect(ix, iy, w - 62, 8, 48, 24)) return handled(SCREEN_TEST_SELECT);
      PinKeypadLayout pk;
      layoutNumericKeypad3x4(w, h, 84, &pk);
      const char* keys = "123456789D0E";
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
          int idx = row * 3 + col;
          int kx = pk.kx + col * (pk.cellW + pk.gap);
          int ky = pk.ky + row * (pk.cellH + pk.gap);
          if (!inRect(ix, iy, kx, ky, pk.cellW, pk.cellH)) continue;
          char k = keys[idx];
          if (k == 'D') {
            if (s_studentInputLen > 0) s_studentInput[--s_studentInputLen] = '\0';
            Screens_draw(tft, current, false);
            return handled(current);
          }
          if (k == 'E') {
            if (!normalizeStudentId(s_studentInput, s_studentId, sizeof(s_studentId))) {
              Buzzer_beepFail();
              Screens_draw(tft, current, false);
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
            Screens_draw(tft, current, false);
            return handled(current);
          }
        }
      }
      break;
    }
    case SCREEN_TEST_FLOW: {
      int count = VerificationSteps_getStepCount((VerifyTestId)s_selectedTestType);
      if (inRect(ix, iy, w - 108, 8, 96, 36)) {
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
        if (inRect(ix, iy, 20, h - 56, w - 40, 48)) {
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
        if (inRect(ix, iy, 20, h - 56, w - 40, 52)) {
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
        int half = (w - 50) / 2, btnY = h - 56;
        const bool expectedYes = VerificationSteps_expectedYesForStep((VerifyTestId)s_selectedTestType, s_stepIndex);
        if (inRect(ix, iy, 20, btnY, half, 52)) {
          const bool answerYes = true;
          if (s_stepIndex == 2 && s_selectedTestType == (int)VERIFY_INSULATION) s_resultIsSheathedHeating = true;
          if (answerYes == expectedYes) {
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
          } else {
            s_flowPhase = 1;
            s_resultPass = false;
            s_resultLabel = VerificationSteps_getTestName((VerifyTestId)s_selectedTestType);
            s_resultUnit = "";
            VerificationSteps_getStep((VerifyTestId)s_selectedTestType, s_stepIndex, &step);
            s_resultClause = step.clause;
            s_resultValue = 0.0f;
            s_testCompletedMs = millis();
            syncTrainingFlowEvent("result_confirmed", false, nullptr);
          }
          Screens_draw(tft, current);
          return handled(current);
        }
        if (inRect(ix, iy, 30 + half, btnY, half, 52)) {
          const bool answerYes = false;
          if (answerYes == expectedYes) {
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
          } else {
            s_flowPhase = 1;
            s_resultPass = false;
            s_resultLabel = VerificationSteps_getTestName((VerifyTestId)s_selectedTestType);
            s_resultUnit = "";
            VerificationSteps_getStep((VerifyTestId)s_selectedTestType, s_stepIndex, &step);
            s_resultClause = step.clause;
            s_resultValue = 0.0f;
            s_testCompletedMs = millis();
            syncTrainingFlowEvent("result_confirmed", false, nullptr);
          }
          Screens_draw(tft, current);
          return handled(current);
        }
      } else if (step.type == STEP_INFO) {
        if (inRect(ix, iy, 20, h - 56, w - 40, 52)) {
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

        const bool showRcdReq = (step.resultKind == RESULT_RCD_MS && s_rcdRequiredMaxMs > 0.0f);
        ResultEntryLayout rel;
        layoutResultEntry(w, h, s_resultEntryYAfterInstr, showRcdReq, unitCount, &rel);

        if (inRect(ix, iy, rel.delX, rel.delY, rel.delW, rel.delH)) {
          if (s_resultInputLen > 0) s_resultInput[--s_resultInputLen] = '\0';
          Screens_draw(tft, current, false);
          return handled(current);
        }

        for (int i = 0; i < unitCount; i++) {
          int ux = 20 + i * (rel.uw + rel.uGap);
          if (inRect(ix, iy, ux, rel.uy, rel.uw, rel.uh)) {
            s_resultInputUnitIdx = i;
            Screens_draw(tft, current, true);
            return handled(current);
          }
        }

        const char* keys[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "OK" };
        for (int row = 0; row < 4; row++) {
          for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            int kx = rel.kx + col * (rel.cellW + rel.gap);
            int ky = rel.ky + row * (rel.cellH + rel.gap);
            if (!inRect(ix, iy, kx, ky, rel.cellW, rel.cellH)) continue;
            const char* key = keys[idx];
            if (strcmp(key, "OK") == 0) {
              if (s_resultInputLen <= 0) {
                Buzzer_beepFail();
                showPrompt(tft, "Enter a value", "before confirming", kRed);
                Screens_draw(tft, current, true);
                return handled(current);
              }
              char* endptr = nullptr;
              float rawValue = strtof(s_resultInput, &endptr);
              if (endptr == s_resultInput || !endptr || *endptr != '\0' || rawValue < 0.0f) {
                Buzzer_beepFail();
                showPrompt(tft, "Invalid value", "Try again", kRed);
                Screens_draw(tft, current, true);
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
            Screens_draw(tft, current, false);
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
      int y = 0, btnH = 0, gap = 0, nRows = 0, backY = 0;
      bool field = AppState_isFieldMode();
      sparkySettingsLayout(w, h, field, &y, &btnH, &gap, &nRows, &backY);
      if (inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_ROTATION);
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_WIFI_LIST);
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) {
        bool enabled = !AppState_getBuzzerEnabled();
        AppState_setBuzzerEnabled(enabled);
        /* Keep toggle lightweight: redraw settings immediately without modal prompt. */
        Screens_draw(tft, current, false);
        return handled(current);
      }
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_ABOUT);
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_UPDATES);
      y += btnH + gap;
      if (!field) {
        if (inRect(ix, iy, 20, y, w - 40, btnH)) {
          Screens_resetPinEntry();
          Screens_setPinSuccessTarget(SCREEN_TRAINING_SYNC);
          Screens_setPinCancelTarget(SCREEN_SETTINGS);
          return handled(SCREEN_PIN_ENTER);
        }
        y += btnH + gap;
      }
      if (inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_EMAIL_SETTINGS);
      y += btnH + gap;
      /* Mode change (boot hold): same destination as long-press on boot logo — PIN then Training/Field. */
      if (inRect(ix, iy, 20, y, w - 40, btnH)) {
        Screens_resetPinEntry();
        Screens_setPinSuccessTarget(SCREEN_MODE_SELECT);
        Screens_setPinCancelTarget(SCREEN_SETTINGS);
        return handled(SCREEN_PIN_ENTER);
      }
      y += btnH + gap;
      if (inRect(ix, iy, 20, y, w - 40, btnH)) {
        ESP.restart();
        return handled(current);
      }
      y += btnH + gap;
      if (!field && inRect(ix, iy, 20, y, w - 40, btnH)) return handled(SCREEN_CHANGE_PIN);
      if (inRect(ix, iy, 20, backY, w - 40, 40)) {
        s_trainingSettingsUnlocked = false;
        return handled(SCREEN_MAIN_MENU);
      }
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
      if (inRect(ix, iy, 20, h - 52, w - 40, 44)) return handled(SCREEN_SETTINGS);
      break;
    case SCREEN_WIFI_LIST: {
      if (s_wifiPortalAssist) {
        if (inRect(ix, iy, 176, 144, w - 196, 34)) {
          bool stillPortal = WifiManager_isCaptivePortalLikely();
          if (stillPortal) {
            WifiManager_getPortalUrl(s_wifiPortalUrl, sizeof(s_wifiPortalUrl));
            showPrompt(tft, "Portal still required", "Complete login on phone", kAccent);
            Screens_draw(tft, current);
          } else {
            s_wifiPortalAssist = false;
            showSavedPrompt(tft, "Internet reachable");
            Screens_draw(tft, current);
          }
          return handled(current);
        }
        if (inRect(ix, iy, 20, h - 48, 124, 40)) {
          s_wifiPortalAssist = false;
          Screens_draw(tft, current);
          return handled(current);
        }
        break;
      }
      const int scanX = 20, scanY = 62, scanW = 124, scanH = 34, rowH = 38;
      if (inRect(ix, iy, scanX, scanY, scanW, scanH)) {
        s_networkCount = WifiManager_scan(s_networks, WIFI_MAX_SSIDS);
        s_wifiPortalAssist = false;
        Screens_draw(tft, current);
        return handled(current);
      }
      int listY = scanY + scanH + 6;
      for (int i = 0; i < s_networkCount && listY + rowH < h - 44; i++) {
        if (inRect(ix, iy, 20, listY, w - 40, rowH)) {
          strncpy(s_selectedSsid, s_networks[i].ssid, WIFI_SSID_LEN - 1);
          s_selectedSsid[WIFI_SSID_LEN - 1] = '\0';
          s_wifiPassLen = 0;
          s_wifiPass[0] = '\0';
          s_wifiPortalAssist = false;
          s_oskLettersMode = true;
          s_oskLetterUpper = false;
          return handled(SCREEN_WIFI_PASSWORD);
        }
        listY += rowH;
      }
      if (inRect(ix, iy, 20, h - 48, 124, 40)) { s_wifiPortalAssist = false; return handled(SCREEN_SETTINGS); }
      break;
    }
    case SCREEN_WIFI_PASSWORD: {
      if (inRect(ix, iy, w - 70, 8, 50, 26)) {
        s_selectedSsid[0] = '\0';
        s_wifiPassLen = 0;
        return handled(SCREEN_WIFI_LIST);
      }
      QwertyKeyboardLayout qk;
      layoutQwertyKeyboard(w, h, 68, 4, 40, &qk);
      {
        const char* rows[4];
        int cols[4];
        if (s_oskLettersMode) {
          rows[0] = kOskLettersRow0;
          cols[0] = 10;
          rows[1] = kOskLettersRow1;
          cols[1] = 9;
          rows[2] = kOskLettersRow2;
          cols[2] = 7;
          rows[3] = kOskLettersRow3Email;
          cols[3] = 4;
        } else {
          rows[0] = kOskNumsRow0;
          cols[0] = 10;
          rows[1] = kOskNumsRow1;
          cols[1] = 8;
          rows[2] = kOskNumsRow2;
          cols[2] = 10;
          rows[3] = kOskNumsRow3;
          cols[3] = 10;
        }
        for (int row = 0; row < 4; row++) {
          for (int col = 0; col < cols[row]; col++) {
            int kx = qk.marginX + col * (qk.cellW + qk.gap);
            int ky = qk.keyStartY + row * (qk.cellH + qk.gap);
            if (inRect(ix, iy, kx, ky, qk.cellW, qk.cellH) && s_wifiPassLen < WIFI_PASS_LEN - 1) {
              char c = rows[row][col];
              if (s_oskLettersMode && row <= 2) c = applyLetterCase(c, s_oskLetterUpper);
              s_wifiPass[s_wifiPassLen++] = c;
              s_wifiPass[s_wifiPassLen] = '\0';
              Screens_draw(tft, current, false);
              return handled(current);
            }
          }
        }
      }
      {
        const int delW = 52, aaW = 44, modeW = 56;
        int aaX = qk.marginX + delW + qk.gap;
        int modeX = aaX + aaW + qk.gap;
        int connX = modeX + modeW + qk.gap;
        int connW = w - connX - qk.marginX;
        if (inRect(ix, iy, aaX, qk.ctrlY, aaW, qk.ctrlRowH)) {
          s_oskLetterUpper = !s_oskLetterUpper;
          Screens_draw(tft, current, true);
          return handled(current);
        }
        if (inRect(ix, iy, modeX, qk.ctrlY, modeW, qk.ctrlRowH)) {
          s_oskLettersMode = !s_oskLettersMode;
          Screens_draw(tft, current, true);
          return handled(current);
        }
        if (inRect(ix, iy, qk.marginX, qk.ctrlY, delW, qk.ctrlRowH)) {
          if (s_wifiPassLen > 0) s_wifiPass[--s_wifiPassLen] = '\0';
          Screens_draw(tft, current, false);
          return handled(current);
        }
        if (inRect(ix, iy, connX, qk.ctrlY, connW, qk.ctrlRowH)) {
        if (WifiManager_connect(s_selectedSsid, s_wifiPassLen > 0 ? s_wifiPass : "")) {
          Buzzer_beepPass();
          bool portal = WifiManager_isCaptivePortalLikely();
          if (portal) {
            WifiManager_getPortalUrl(s_wifiPortalUrl, sizeof(s_wifiPortalUrl));
            s_wifiPortalAssist = true;
            showPrompt(tft, "Portal login needed", "Open assist screen", kAccent);
          } else {
            s_wifiPortalAssist = false;
            showPrompt(tft, "Wi-Fi connected", s_selectedSsid, kGreen);
          }
        } else {
          s_wifiPortalAssist = false;
          Buzzer_beepFail();
          showPrompt(tft, "Wi-Fi connect failed", "Check password", kRed);
        }
        s_selectedSsid[0] = '\0';
        s_wifiPassLen = 0;
        return handled(SCREEN_WIFI_LIST);
        }
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
      if (inRect(ix, iy, 20, btnY, 80, 26)) return handled(SCREEN_SETTINGS);
      break;
    }
    case SCREEN_TRAINING_SYNC: {
      if (AppState_isFieldMode()) {
        if (inRect(ix, iy, 20, h - 38, 80, 30)) return handled(SCREEN_SETTINGS);
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
      if (inRect(ix, iy, 20, y, 80, btnH)) return handled(SCREEN_SETTINGS);
      break;
    }
    case SCREEN_TRAINING_SYNC_EDIT: {
      if (inRect(ix, iy, w - 62, 6, 48, 24)) return handled(SCREEN_TRAINING_SYNC);
      QwertyKeyboardLayout qk;
      layoutQwertyKeyboard(w, h, 48, 4, 40, &qk);
      int maxLen = syncEditMaxLen();
      {
        const char* rows[4];
        int cols[4];
        if (s_oskLettersMode) {
          rows[0] = kOskLettersRow0;
          cols[0] = 10;
          rows[1] = kOskLettersRow1;
          cols[1] = 9;
          rows[2] = kOskLettersRow2;
          cols[2] = 7;
          rows[3] = kOskLettersRow3Sync;
          cols[3] = 8;
        } else {
          rows[0] = kOskNumsRow0;
          cols[0] = 10;
          rows[1] = kOskNumsRow1;
          cols[1] = 8;
          rows[2] = kOskNumsRow2;
          cols[2] = 10;
          rows[3] = kOskNumsRow3;
          cols[3] = 10;
        }
        for (int row = 0; row < 4; row++) {
          for (int col = 0; col < cols[row]; col++) {
            int kx = qk.marginX + col * (qk.cellW + qk.gap);
            int ky = qk.keyStartY + row * (qk.cellH + qk.gap);
            if (!inRect(ix, iy, kx, ky, qk.cellW, qk.cellH) || s_syncEditLen >= maxLen - 1) continue;
            char c = rows[row][col];
            if (s_oskLettersMode && row <= 2) c = applyLetterCase(c, s_oskLetterUpper);
            s_syncEditBuffer[s_syncEditLen++] = c;
            s_syncEditBuffer[s_syncEditLen] = '\0';
            Screens_draw(tft, current, false);
            return handled(current);
          }
        }
      }
      {
        const int delW = 52, aaW = 44, modeW = 56;
        int aaX = qk.marginX + delW + qk.gap;
        int modeX = aaX + aaW + qk.gap;
        int saveX = modeX + modeW + qk.gap;
        int saveW = w - saveX - qk.marginX;
        if (inRect(ix, iy, modeX, qk.ctrlY, modeW, qk.ctrlRowH)) {
          s_oskLettersMode = !s_oskLettersMode;
          Screens_draw(tft, current, true);
          return handled(current);
        }
        if (inRect(ix, iy, qk.marginX, qk.ctrlY, delW, qk.ctrlRowH)) {
          if (s_syncEditLen > 0) s_syncEditBuffer[--s_syncEditLen] = '\0';
          Screens_draw(tft, current, false);
          return handled(current);
        }
        if (inRect(ix, iy, aaX, qk.ctrlY, aaW, qk.ctrlRowH)) {
          s_oskLetterUpper = !s_oskLetterUpper;
          Screens_draw(tft, current, true);
          return handled(current);
        }
        if (inRect(ix, iy, saveX, qk.ctrlY, saveW, qk.ctrlRowH)) {
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
      }
      break;
    }
    case SCREEN_EMAIL_SETTINGS: {
      const int rowY0 = 34;
      const int rowH = 20;
      for (int i = 0; i < 5; i++) {
        if (inRect(ix, iy, 20, rowY0 + i * rowH - 2, w - 40, rowH + 2)) {
          s_editingEmailField = i;
          s_oskLettersMode = true;
          s_oskLetterUpper = false;
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
      if (inRect(ix, iy, 20, h - 72, w - 40, 26)) {
        char err[160] = "";
        if (EmailTest_sendNow(err, sizeof(err)))
          showSavedPrompt(tft, "Test email sent");
        else {
          char msg[96];
          snprintf(msg, sizeof(msg), "Email failed: %s", err[0] ? err : "error");
          showSavedPrompt(tft, msg);
        }
        return handled(SCREEN_EMAIL_SETTINGS);
      }
      if (inRect(ix, iy, 20, h - 38, 80, 30)) return handled(SCREEN_SETTINGS);
      break;
    }
    case SCREEN_EMAIL_FIELD_EDIT: {
      if (inRect(ix, iy, w - 62, 6, 48, 24)) return handled(SCREEN_EMAIL_SETTINGS);
      if (!emailFieldNeedsQwertyOsk(s_editingEmailField)) {
        PinKeypadLayout pk;
        layoutNumericKeypad3x4(w, h, 84, &pk);
        const char* keys = "123456789D0E";
        for (int row = 0; row < 4; row++) {
          for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            int kx = pk.kx + col * (pk.cellW + pk.gap);
            int ky = pk.ky + row * (pk.cellH + pk.gap);
            if (!inRect(ix, iy, kx, ky, pk.cellW, pk.cellH)) continue;
            char k = keys[idx];
            if (k == 'D') {
              if (s_editLen > 0) s_editBuffer[--s_editLen] = '\0';
              Screens_draw(tft, current, false);
              return handled(current);
            }
            if (k == 'E') {
              applyEmailFieldSave(tft);
              return handled(SCREEN_EMAIL_SETTINGS);
            }
            if (k >= '0' && k <= '9' && s_editLen < kSmtpPortMaxDigits &&
                s_editLen < (int)(APP_STATE_EMAIL_STR_LEN - 1)) {
              s_editBuffer[s_editLen++] = k;
              s_editBuffer[s_editLen] = '\0';
              Screens_draw(tft, current, false);
              return handled(current);
            }
          }
        }
        break;
      }
      QwertyKeyboardLayout qk;
      layoutQwertyKeyboard(w, h, 48, 4, 40, &qk);
      {
        const char* rows[4];
        int cols[4];
        if (s_oskLettersMode) {
          rows[0] = kOskLettersRow0;
          cols[0] = 10;
          rows[1] = kOskLettersRow1;
          cols[1] = 9;
          rows[2] = kOskLettersRow2;
          cols[2] = 7;
          rows[3] = kOskLettersRow3Email;
          cols[3] = 4;
        } else {
          rows[0] = kOskNumsRow0;
          cols[0] = 10;
          rows[1] = kOskNumsRow1;
          cols[1] = 8;
          rows[2] = kOskNumsRow2;
          cols[2] = 10;
          rows[3] = kOskNumsRow3;
          cols[3] = 10;
        }
        for (int row = 0; row < 4; row++) {
          for (int col = 0; col < cols[row]; col++) {
            int kx = qk.marginX + col * (qk.cellW + qk.gap);
            int ky = qk.keyStartY + row * (qk.cellH + qk.gap);
            if (inRect(ix, iy, kx, ky, qk.cellW, qk.cellH) && s_editLen < (int)(APP_STATE_EMAIL_STR_LEN - 1)) {
              char c = rows[row][col];
              if (s_oskLettersMode && row <= 2) c = applyLetterCase(c, s_oskLetterUpper);
              s_editBuffer[s_editLen++] = c;
              s_editBuffer[s_editLen] = '\0';
              Screens_draw(tft, current, false);
              return handled(current);
            }
          }
        }
      }
      {
        const int delW = 52, aaW = 44, modeW = 56;
        int aaX = qk.marginX + delW + qk.gap;
        int modeX = aaX + aaW + qk.gap;
        int saveX = modeX + modeW + qk.gap;
        int saveW = w - saveX - qk.marginX;
        if (inRect(ix, iy, aaX, qk.ctrlY, aaW, qk.ctrlRowH)) {
          s_oskLetterUpper = !s_oskLetterUpper;
          Screens_draw(tft, current, true);
          return handled(current);
        }
        if (inRect(ix, iy, modeX, qk.ctrlY, modeW, qk.ctrlRowH)) {
          s_oskLettersMode = !s_oskLettersMode;
          Screens_draw(tft, current, true);
          return handled(current);
        }
        if (inRect(ix, iy, qk.marginX, qk.ctrlY, delW, qk.ctrlRowH)) {
          if (s_editLen > 0) s_editBuffer[--s_editLen] = '\0';
          Screens_draw(tft, current, false);
          return handled(current);
        }
        if (inRect(ix, iy, saveX, qk.ctrlY, saveW, qk.ctrlRowH)) {
          applyEmailFieldSave(tft);
          return handled(SCREEN_EMAIL_SETTINGS);
        }
      }
      break;
    }
    case SCREEN_PIN_ENTER: {
      PinKeypadLayout pk;
      layoutPinKeypadGeometry(w, h, false, &pk);
      if (inRect(ix, iy, pk.backX, pk.backY, pk.backW, pk.backH)) {
        Screens_resetPinEntry();
        return handled(s_pinCancelTarget);
      }
      const char* keys = "123456789C0E";
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
          int idx = row * 3 + col;
          int kx = pk.kx + col * (pk.cellW + pk.gap);
          int ky = pk.ky + row * (pk.cellH + pk.gap);
          if (!inRect(ix, iy, kx, ky, pk.cellW, pk.cellH)) continue;
          if (keys[idx] == 'C') {
            Screens_resetPinEntry();
            Screens_draw(tft, current, false);
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
              Screens_draw(tft, current, false);
              return handled(current);
            }
          }
          if (keys[idx] >= '0' && keys[idx] <= '9' && s_pinLen < kPinMaxLen) {
            s_pinDigits[s_pinLen++] = keys[idx];
            s_pinDigits[s_pinLen] = '\0';
            Screens_draw(tft, current, false);
            return handled(current);
          }
        }
      }
      break;
    }
    case SCREEN_CHANGE_PIN: {
      PinKeypadLayout pk;
      layoutPinKeypadGeometry(w, h, true, &pk);
      if (inRect(ix, iy, pk.backX, pk.backY, pk.backW, pk.backH)) {
        s_changePinStep = 0;
        s_pinLen = 0;
        s_pinDigits[0] = '\0';
        s_newPinBuf[0] = '\0';
        s_newPinLen = 0;
        return handled(SCREEN_SETTINGS);
      }
      const char* keys = "123456789C0E";
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
          int idx = row * 3 + col;
          int kx = pk.kx + col * (pk.cellW + pk.gap);
          int ky = pk.ky + row * (pk.cellH + pk.gap);
          if (!inRect(ix, iy, kx, ky, pk.cellW, pk.cellH)) continue;
          if (keys[idx] == 'C') {
            s_pinLen = 0; s_pinDigits[0] = '\0';
            Screens_draw(tft, current, false);
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
              Screens_draw(tft, current, true);
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
            Screens_draw(tft, current, false);
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

ScreenId Screens_handleTouchDrag(SparkyTft* tft, ScreenId current, uint16_t x, uint16_t y) {
  (void)x;
  s_handledButton = false;
  if (!tft || current != SCREEN_TEST_SELECT || !s_testSelectTouchActive) return current;
  int w = getW(tft);
  int h = getH(tft);
  int iy = (int)y;
  unsigned long now = millis();
  if ((long)(now - s_testSelectLastTouchMs) > 3000) {
    sparkyResetTestSelectGesture();
    return current;
  }
  int dy = iy - s_testSelectLastY;
  s_testSelectLastY = iy;
  s_testSelectAccumDy += dy;
  s_testSelectLastTouchMs = now;
  if (abs(s_testSelectAccumDy) >= 10 || abs(dy) >= 6) {
    s_testSelectScrollPx += dy;
    sparkyClampTestSelectScroll(w, h, &s_testSelectScrollPx);
    s_testSelectTapCandidate = -1;
    Screens_draw(tft, current, false);
  }
  return current;
}

ScreenId Screens_handleTouchEnd(SparkyTft* tft, ScreenId current, uint16_t x, uint16_t y) {
  (void)x;
  (void)y;
  s_handledButton = false;
  if (!tft || current != SCREEN_TEST_SELECT || !s_testSelectTouchActive) return current;
  unsigned long now = millis();
  /* Use row from touch-down (not lift), so EEZ synthetic press coords + real finger release stay consistent. */
  bool tapped = (s_testSelectTapCandidate >= 0 && abs(s_testSelectAccumDy) < 10 &&
                 (long)(now - s_testSelectTapCandidateMs) <= 800);
  if (!tapped) {
    sparkyResetTestSelectGesture();
    return current;
  }
  int row = s_testSelectTapCandidate;
  ScreenId next = sparkyCommitTestSelectRow(row);
  if (next != current) s_handledButton = true;
  return next;
}
