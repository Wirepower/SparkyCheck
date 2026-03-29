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
#include "SparkyRtc.h"
#include "SparkyTime.h"
#include "SparkyTzPresets.h"
#include "SparkyNtp.h"
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
#include "EezMockupUi.h"
#endif
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

/**
 * Baseline Y for centered size-2 titles. Status bar draws battery (~y7–17), Wi‑Fi (~y16), clock (y8–22);
 * size-2 glyphs extend ~12px above baseline, so 28 overlapped the banner; 40 clears it before Screens_drawStatusBar().
 */
static const int kScreenTitleYCenterSize2 = 40;

/** Readable text on 4.3" landscape or tall portrait (matches EEZ layout helpers). */
static bool sparkyReadableUi(int w, int h) {
  return (w >= 700) || (h >= 700) || (w >= 480 && h >= 600);
}

/** Narrow width (e.g. portrait 480×800): title must sit below Back, not on the same row. */
static bool sparkyTestFlowNarrowHeader(int w) {
  return w < 600;
}

/** First tap on Install shows flicker warning; second tap starts OTA. Cleared when leaving the flow. */
static bool s_otaInstallConfirmArmed = false;

static void sparkyOtaDisarmInstallConfirm(void) {
  s_otaInstallConfirmArmed = false;
}

/**
 * Firmware updates: bottom-up = Back, toggles, optional full-width Install (pending, no banner),
 * optional Install|Not now row (auto-check offer), Check now.
 */
static void sparkyOtaUpdatesComputeLayout(int w, int h, bool readable, bool showInstallOffer,
                                          bool showStandaloneInstall, int* outCheckY, int* outOfferY,
                                          int* outInstallY, int* outToggleY, int* outBtnH, int* outGap,
                                          int* outHalf, int* outBackY, int* outBackH) {
  *outBtnH = readable ? 42 : 30;
  *outGap = readable ? 8 : 6;
  *outBackH = readable ? 44 : 36;
  const int bottomPad = readable ? 8 : 6;
  *outBackY = h - *outBackH - bottomPad;
  *outToggleY = *outBackY - *outGap - *outBtnH;
  int y = *outToggleY;
  *outInstallY = -1;
  if (showStandaloneInstall) {
    y -= *outGap + *outBtnH;
    *outInstallY = y;
  }
  *outOfferY = -1;
  if (showInstallOffer) {
    y -= *outGap + *outBtnH;
    *outOfferY = y;
  }
  y -= *outGap + *outBtnH;
  *outCheckY = y;
  *outHalf = (w - 50) / 2;
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

static const int kTestSelectPerPage = 5;

static int sparkyTestSelectNumPages(int n) {
  if (n <= 0) return 1;
  return (n + kTestSelectPerPage - 1) / kTestSelectPerPage;
}

typedef struct {
  int rowY[5];
  int rowH;
  int navY, navH;
  int prevX, prevW, midX, midW, nextX, nextW;
} SparkyTestSelectPagedLayout;

static void sparkyTestSelectComputePagedLayout(int w, int h, SparkyTestSelectPagedLayout* L) {
  const bool panelWide = (w >= 700);
  int listTop = panelWide ? 114 : ((h >= 700) ? 106 : 90);
  int navH = panelWide ? 48 : 44;
  int navY = h - navH - 6;
  int listBottom = navY - 8;
  int gap = (listBottom - listTop > 220) ? 10 : 6;
  int usable = listBottom - listTop;
  int rowH = (usable - 4 * gap) / 5;
  if (rowH < 28) {
    gap = 4;
    usable = listBottom - listTop;
    rowH = (usable - 4 * gap) / 5;
  }
  if (rowH < 24) rowH = 24;
  for (int i = 0; i < 5; i++)
    L->rowY[i] = listTop + i * (rowH + gap);
  L->rowH = rowH;
  L->navY = navY;
  L->navH = navH;
  const int marginX = 20;
  const int innerW = w - 2 * marginX;
  const int gapN = 8;
  int cell = (innerW - 2 * gapN) / 3;
  if (cell < 56) cell = 56;
  if (3 * cell + 2 * gapN > innerW) cell = (innerW - 2 * gapN) / 3;
  int totalW = 3 * cell + 2 * gapN;
  int startX = marginX + (innerW - totalW) / 2;
  L->prevX = startX;
  L->prevW = cell;
  L->midX = startX + cell + gapN;
  L->midW = cell;
  L->nextX = startX + 2 * (cell + gapN);
  L->nextW = cell;
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

/** Left-aligned word wrap; does not draw past maxY (avoids status text overlapping OTA buttons). */
static int sparkyDrawWrappedLeftUntilY(SparkyTft* tft, const char* text, int x, int y, int maxW, int maxY, uint8_t ts,
                                       int lineH, uint16_t fg, uint16_t bg) {
  if (!text || !text[0] || y >= maxY || maxW < 24) return y;
  tft->setTextWrap(false);
  tft->setTextSize(ts);
  tft->setTextColor(fg, bg);
  const int cw = 6 * (int)ts;
  if (maxW < cw * 4) return y;

  /* One wrapped output line; wide panels + ts=1 can exceed OTA_STATUS_LEN chars per line. */
  char lineBuf[192];
  lineBuf[0] = '\0';
  const char* p = text;

  while (*p) {
    if (y + lineH > maxY) break;
    while (*p == ' ') p++;
    if (!*p) break;
    char word[96];
    int wl = 0;
    while (*p && *p != ' ' && wl < (int)sizeof(word) - 1) word[wl++] = *p++;
    word[wl] = '\0';
    if (wl <= 0) continue;

    int cur = (int)strlen(lineBuf);
    int need = cur + (cur > 0 ? 1 : 0) + wl;
    if (cur > 0 && need * cw > maxW) {
      if (lineBuf[0]) {
        if (y + lineH > maxY) break;
        tft->setCursor(x, y);
        tft->print(lineBuf);
        y += lineH;
        lineBuf[0] = '\0';
      }
      if (y + lineH > maxY) break;
    }

    if (wl * cw > maxW) {
      for (int i = 0; i < wl; i++) {
        char one[2] = {word[i], '\0'};
        cur = (int)strlen(lineBuf);
        if (cur > 0 && (cur + 1) * cw > maxW) {
          if (y + lineH > maxY) break;
          tft->setCursor(x, y);
          tft->print(lineBuf);
          y += lineH;
          lineBuf[0] = '\0';
        }
        if (y + lineH > maxY) break;
        strncat(lineBuf, one, sizeof(lineBuf) - strlen(lineBuf) - 1);
      }
      continue;
    }

    cur = (int)strlen(lineBuf);
    if (cur + (cur > 0 ? 1 : 0) + wl >= (int)sizeof(lineBuf) - 1) {
      if (lineBuf[0]) {
        if (y + lineH > maxY) break;
        tft->setCursor(x, y);
        tft->print(lineBuf);
        y += lineH;
        lineBuf[0] = '\0';
      }
      if (y + lineH > maxY) break;
    }
    if (lineBuf[0]) strncat(lineBuf, " ", sizeof(lineBuf) - strlen(lineBuf) - 1);
    strncat(lineBuf, word, sizeof(lineBuf) - strlen(lineBuf) - 1);
  }
  if (lineBuf[0] && y + lineH <= maxY) {
    tft->setCursor(x, y);
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

/** Two centered lines in a settings row (Wi‑Fi + IP, buzzer + state). */
static void sparkyDrawDualLineSettingRow(SparkyTft* tft, int x, int y, int bw, int bh, const char* line1,
                                         const char* line2, uint8_t ts1, uint8_t ts2) {
  if (!line1) line1 = "";
  if (!line2) line2 = "";
  tft->setTextWrap(false);
  int h1 = 7 * (int)ts1;
  int h2 = 7 * (int)ts2;
  const int g = 2;
  int total = h1 + g + h2;
  int ty = y + (bh - total) / 2;
  if (ty < y + 2) ty = y + 2;
  tft->setTextColor(kWhite, kBtn);
  tft->setTextSize(ts1);
  int w1 = (int)strlen(line1) * 6 * (int)ts1;
  tft->setCursor(x + (bw - w1) / 2, ty);
  tft->print(line1);
  tft->setTextSize(ts2);
  int w2 = (int)strlen(line2) * 6 * (int)ts2;
  tft->setCursor(x + (bw - w2) / 2, ty + h1 + g);
  tft->print(line2);
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

static int s_testSelectPage = 0;

static void sparkyTestSelectClampPage(int n) {
  int p = sparkyTestSelectNumPages(n);
  if (s_testSelectPage >= p) s_testSelectPage = p - 1;
  if (s_testSelectPage < 0) s_testSelectPage = 0;
}

static void sparkyDrawTestSelectPagedList(SparkyTft* tft, int w, int h) {
  const int n = VerificationSteps_getActiveTestCount();
  sparkyTestSelectClampPage(n);
  const int pages = sparkyTestSelectNumPages(n);
  SparkyTestSelectPagedLayout L;
  sparkyTestSelectComputePagedLayout(w, h, &L);
  const bool panelWide = (w >= 700);
  const bool readable = sparkyReadableUi(w, h);
  uint8_t ts = panelWide ? (uint8_t)2 : (readable ? (uint8_t)2 : (uint8_t)1);
  const uint16_t kSlotOutline = 0x4A69;

  int first = s_testSelectPage * kTestSelectPerPage;
  for (int slot = 0; slot < kTestSelectPerPage; slot++) {
    int idx = first + slot;
    int y = L.rowY[slot];
    int bh = L.rowH - 2;
    if (idx < n) {
      tft->fillRoundRect(20, y, w - 40, bh, 6, kBtn);
      tft->drawRoundRect(20, y, w - 40, bh, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      sparkyDrawBtnLabel(tft, 20, y, w - 40, bh, sparkyDeviceTestLabel(VerificationSteps_getTestName((VerifyTestId)idx)), ts);
    } else {
      tft->fillRoundRect(20, y, w - 40, bh, 6, kBg);
      tft->drawRoundRect(20, y, w - 40, bh, 6, kSlotOutline);
    }
  }

  bool prevEn = (s_testSelectPage > 0);
  bool nextEn = (s_testSelectPage < pages - 1);
  uint16_t prevBg = prevEn ? kBtn : (uint16_t)0x18C3;
  uint16_t nextBg = nextEn ? kBtn : (uint16_t)0x18C3;

  tft->fillRoundRect(L.prevX, L.navY, L.prevW, L.navH, 6, prevBg);
  tft->drawRoundRect(L.prevX, L.navY, L.prevW, L.navH, 6, kWhite);
  tft->setTextColor(kWhite, prevBg);
  sparkyDrawBtnLabel(tft, L.prevX, L.navY, L.prevW, L.navH, "PREV", 2);

  tft->fillRect(L.midX, L.navY, L.midW, L.navH, kBg);
  char pg[12];
  snprintf(pg, sizeof(pg), "%d/%d", s_testSelectPage + 1, pages);
  tft->setTextSize(2);
  tft->setTextColor(kWhite, kBg);
  int ptw = (int)strlen(pg) * 12;
  tft->setCursor(L.midX + (L.midW - ptw) / 2, L.navY + (L.navH - 14) / 2);
  tft->print(pg);

  tft->fillRoundRect(L.nextX, L.navY, L.nextW, L.navH, 6, nextBg);
  tft->drawRoundRect(L.nextX, L.navY, L.nextW, L.navH, 6, kWhite);
  tft->setTextColor(kWhite, nextBg);
  sparkyDrawBtnLabel(tft, L.nextX, L.navY, L.nextW, L.navH, "NEXT", 2);
}

void Screens_drawTestSelectPagedContent(SparkyTft* tft) {
  if (!tft) return;
  sparkyDrawTestSelectPagedList(tft, tft->width(), tft->height());
}

static int s_settingsPage = 0;

static int settingsRowCount(bool field) { return field ? 9 : 11; }

static int settingsNumPages(bool field) {
  int n = settingsRowCount(field);
  int p = (n + kTestSelectPerPage - 1) / kTestSelectPerPage;
  return p < 1 ? 1 : p;
}

static void settingsClampPage(bool field) {
  int p = settingsNumPages(field);
  if (s_settingsPage >= p) s_settingsPage = p - 1;
  if (s_settingsPage < 0) s_settingsPage = 0;
}

static const char* settingsRowPrimaryLabel(bool field, int row) {
  if (field) {
    switch (row) {
      case 0: return "Screen rotation";
      case 1: return "WiFi connection";
      case 2: return "Buzzer (sound)";
      case 3: return "Date & time";
      case 4: return "About";
      case 5: return "Firmware updates";
      case 6: return "Email settings";
      case 7: return "Mode change (boot hold)";
      case 8: return "Restart device";
      default: return "";
    }
  }
  switch (row) {
    case 0: return "Screen rotation";
    case 1: return "WiFi connection";
    case 2: return "Buzzer (sound)";
    case 3: return "Date & time";
    case 4: return "About";
    case 5: return "Firmware updates";
    case 6: return "Training sync (PIN)";
    case 7: return "Email settings";
    case 8: return "Mode change (boot hold)";
    case 9: return "Restart device";
    case 10: return "Change PIN";
    default: return "";
  }
}

static ScreenId settingsRowNavigate(SparkyTft* tft, bool field, int row) {
  (void)tft;
  if (field) {
    if (row == 0) return SCREEN_ROTATION;
    if (row == 1) return SCREEN_WIFI_LIST;
    if (row == 2) {
      AppState_setBuzzerEnabled(!AppState_getBuzzerEnabled());
      return SCREEN_SETTINGS;
    }
    if (row == 3) return SCREEN_DATE_TIME;
    if (row == 4) return SCREEN_ABOUT;
    if (row == 5) return SCREEN_UPDATES;
    if (row == 6) return SCREEN_EMAIL_SETTINGS;
    if (row == 7) {
      Screens_resetPinEntry();
      Screens_setPinSuccessTarget(SCREEN_MODE_SELECT);
      Screens_setPinCancelTarget(SCREEN_SETTINGS);
      return SCREEN_PIN_ENTER;
    }
    if (row == 8) {
      ESP.restart();
      return SCREEN_SETTINGS;
    }
  } else {
    if (row == 0) return SCREEN_ROTATION;
    if (row == 1) return SCREEN_WIFI_LIST;
    if (row == 2) {
      AppState_setBuzzerEnabled(!AppState_getBuzzerEnabled());
      return SCREEN_SETTINGS;
    }
    if (row == 3) return SCREEN_DATE_TIME;
    if (row == 4) return SCREEN_ABOUT;
    if (row == 5) return SCREEN_UPDATES;
    if (row == 6) {
      Screens_resetPinEntry();
      Screens_setPinSuccessTarget(SCREEN_TRAINING_SYNC);
      Screens_setPinCancelTarget(SCREEN_SETTINGS);
      return SCREEN_PIN_ENTER;
    }
    if (row == 7) return SCREEN_EMAIL_SETTINGS;
    if (row == 8) {
      Screens_resetPinEntry();
      Screens_setPinSuccessTarget(SCREEN_MODE_SELECT);
      Screens_setPinCancelTarget(SCREEN_SETTINGS);
      return SCREEN_PIN_ENTER;
    }
    if (row == 9) {
      ESP.restart();
      return SCREEN_SETTINGS;
    }
    if (row == 10) return SCREEN_CHANGE_PIN;
  }
  return SCREEN_SETTINGS;
}

static void sparkyDrawSettingsPagedList(SparkyTft* tft, int w, int h) {
  bool field = AppState_isFieldMode();
  settingsClampPage(field);
  const int n = settingsRowCount(field);
  const int pages = settingsNumPages(field);
  SparkyTestSelectPagedLayout L;
  sparkyTestSelectComputePagedLayout(w, h, &L);
  const uint8_t lblTs = sparkyReadableUi(w, h) ? (uint8_t)2 : (uint8_t)1;

  int first = s_settingsPage * kTestSelectPerPage;
  for (int slot = 0; slot < kTestSelectPerPage; slot++) {
    int idx = first + slot;
    int y = L.rowY[slot];
    int bh = L.rowH - 2;
    if (idx < n) {
      tft->fillRoundRect(20, y, w - 40, bh, 6, kBtn);
      tft->drawRoundRect(20, y, w - 40, bh, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      if (idx == 1) {
        char ipLine[24];
        if (WifiManager_isConnected()) {
          WifiManager_getIpString(ipLine, sizeof(ipLine));
        } else {
          strncpy(ipLine, "Not connected", sizeof(ipLine) - 1);
          ipLine[sizeof(ipLine) - 1] = '\0';
        }
        sparkyDrawDualLineSettingRow(tft, 20, y, w - 40, bh, "WiFi connection", ipLine, lblTs, 1);
      } else if (idx == 2) {
        sparkyDrawDualLineSettingRow(tft, 20, y, w - 40, bh, "Buzzer (sound)",
                                     AppState_getBuzzerEnabled() ? "On" : "Off", lblTs, 1);
      } else if (idx == 5) {
        sparkyDrawDualLineSettingRow(tft, 20, y, w - 40, bh, "Firmware updates",
                                     OtaUpdate_getCurrentVersion(), lblTs, 1);
      } else {
        sparkyDrawBtnLabel(tft, 20, y, w - 40, bh, settingsRowPrimaryLabel(field, idx), lblTs);
      }
    } else {
      tft->fillRoundRect(20, y, w - 40, bh, 6, kBg);
      tft->drawRoundRect(20, y, w - 40, bh, 6, (uint16_t)0x4A69);
    }
  }

  bool prevEn = (s_settingsPage > 0);
  bool nextEn = (s_settingsPage < pages - 1);
  uint16_t prevBg = prevEn ? kBtn : (uint16_t)0x18C3;
  uint16_t nextBg = nextEn ? kBtn : (uint16_t)0x18C3;
  tft->fillRoundRect(L.prevX, L.navY, L.prevW, L.navH, 6, prevBg);
  tft->drawRoundRect(L.prevX, L.navY, L.prevW, L.navH, 6, kWhite);
  tft->setTextColor(kWhite, prevBg);
  sparkyDrawBtnLabel(tft, L.prevX, L.navY, L.prevW, L.navH, "PREV", 2);
  tft->fillRect(L.midX, L.navY, L.midW, L.navH, kBg);
  char pg[12];
  snprintf(pg, sizeof(pg), "%d/%d", s_settingsPage + 1, pages);
  tft->setTextSize(2);
  tft->setTextColor(kWhite, kBg);
  int ptw = (int)strlen(pg) * 12;
  tft->setCursor(L.midX + (L.midW - ptw) / 2, L.navY + (L.navH - 14) / 2);
  tft->print(pg);
  tft->fillRoundRect(L.nextX, L.navY, L.nextW, L.navH, 6, nextBg);
  tft->drawRoundRect(L.nextX, L.navY, L.nextW, L.navH, 6, kWhite);
  tft->setTextColor(kWhite, nextBg);
  sparkyDrawBtnLabel(tft, L.nextX, L.navY, L.nextW, L.navH, "NEXT", 2);
}

void Screens_drawSettingsPagedContent(SparkyTft* tft) {
  if (!tft) return;
  sparkyDrawSettingsPagedList(tft, tft->width(), tft->height());
}

/* Full clock edit (same fields as admin web form). */
static int s_clockWizardStep = 0;
static int s_clockWizDay = 1, s_clockWizMon = 1, s_clockWizYear = 2026;
static int s_clockWizHour = 12, s_clockWizMin = 0, s_clockWizSec = 0;
static char s_clockWizInput[12];
static int s_clockWizInputLen = 0;

static void clockWizardLoadFromClock(void) {
  time_t t = time(nullptr);
  time_t w = SparkyTime_utcToWallTime(t);
  struct tm lt;
  if (gmtime_r(&w, &lt)) {
    s_clockWizDay = lt.tm_mday;
    s_clockWizMon = lt.tm_mon + 1;
    s_clockWizYear = lt.tm_year + 1900;
    s_clockWizHour = lt.tm_hour;
    s_clockWizMin = lt.tm_min;
    s_clockWizSec = lt.tm_sec;
  }
}

static void clockWizardPrefillInput(void) {
  s_clockWizInput[0] = '\0';
  int v = 0;
  switch (s_clockWizardStep) {
    case 0: v = s_clockWizDay; break;
    case 1: v = s_clockWizMon; break;
    case 2: v = s_clockWizYear; break;
    case 3: v = s_clockWizHour; break;
    case 4: v = s_clockWizMin; break;
    case 5: v = s_clockWizSec; break;
    default: return;
  }
  if (s_clockWizardStep == 2)
    snprintf(s_clockWizInput, sizeof(s_clockWizInput), "%d", v);
  else
    snprintf(s_clockWizInput, sizeof(s_clockWizInput), "%02d", v);
  s_clockWizInputLen = (int)strlen(s_clockWizInput);
}

static void clockWizardBegin(void) {
  clockWizardLoadFromClock();
  s_clockWizardStep = 0;
  clockWizardPrefillInput();
}

static void sparkyCycleTimezonePreset(void) {
  unsigned idx = SparkyTzPresets_indexForOffset(AppState_getClockTzOffsetMinutes());
  const unsigned n = SparkyTzPresets_count();
  if (n == 0) return;
  if (idx >= n) idx = 0;
  else
    idx = (idx + 1) % n;
  const SparkyTzPreset* p = SparkyTzPresets_get(idx);
  if (p) AppState_setClockTzOffsetMinutes(p->offsetMinutes);
}

static const char* clockWizardFieldTitle(int step) {
  switch (step) {
    case 0: return "Day (dd)";
    case 1: return "Month (mm)";
    case 2: return "Year (yyyy)";
    case 3: return "Hour (0-23)";
    case 4: return "Minute";
    case 5: return "Second";
    default: return "";
  }
}

static bool clockWizardCommitStep(int* outVal) {
  if (!s_clockWizInput[0]) return false;
  int v = atoi(s_clockWizInput);
  switch (s_clockWizardStep) {
    case 0:
      if (v < 1 || v > 31) return false;
      break;
    case 1:
      if (v < 1 || v > 12) return false;
      break;
    case 2:
      if (v < 2000 || v > 2099) return false;
      break;
    case 3:
      if (v < 0 || v > 23) return false;
      break;
    case 4:
    case 5:
      if (v < 0 || v > 59) return false;
      break;
    default:
      return false;
  }
  *outVal = v;
  return true;
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

  /* Keep the numeric keypad out from under the Del button (same row band as value). */
  const int keypadRight = L->delX - 10;
  int availH = h - bottomPad - L->ky;
  if (availH < 4 * 44 + 3 * L->gap) {
    L->uh = 36;
    L->ky = L->uy + L->uh + 8;
    availH = h - bottomPad - L->ky;
  }
  if (availH < 0) availH = 0;
  if (keypadRight > marginX + 2 * L->gap + 60)
    L->cellW = (keypadRight - marginX - 2 * L->gap) / 3;
  else
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

static char s_reportSavedBasename[64] = "";
static int s_reportListPage = 0;
/** Bit i = report index i marked for delete (max 32 reports). */
static uint32_t s_reportDeleteMask = 0;
static const int kReportListPerPage = 5;
/** Width reserved at left of each row for delete checkbox hit target. */
static const int kReportCheckCol = 48;
static char s_reportListNames[32][64];
static int s_reportListCount = 0;

static char s_reportViewBasename[64] = "";
static char s_reportViewCsv[12288];
static int s_reportViewLineOff[256];
static int s_reportViewLineCount = 0;
static int s_reportViewScrollLine = 0;
static int s_modeSelectChoice = 0;  /* 0 = Training, 1 = Field */
/** Bottom of instruction text on STEP_RESULT_ENTRY (for layoutResultEntry in draw + touch). */
static int s_resultEntryYAfterInstr = 0;

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

static void sparkyRefreshReportListCache(void) {
  s_reportListCount = ReportGenerator_fillBasenameList(s_reportListNames, 32);
  int pages = (s_reportListCount + kReportListPerPage - 1) / kReportListPerPage;
  if (pages < 1) pages = 1;
  if (s_reportListPage >= pages) s_reportListPage = pages - 1;
  if (s_reportListPage < 0) s_reportListPage = 0;
  for (int i = s_reportListCount; i < 32; i++) s_reportDeleteMask &= ~(1u << i);
}

typedef struct {
  int rowY[5];
  int rowH;
  int navY, navH;
  int backX, backY, backW, backH;
  int delX, delY, delW, delH;
  int prevX, prevW, midX, midW, nextX, nextW;
  int pages;
  bool showPager;
} ReportListLayout;

static void sparkyReportListComputeLayout(int w, int h, bool fieldMode, ReportListLayout* L) {
  L->pages = (s_reportListCount + kReportListPerPage - 1) / kReportListPerPage;
  if (L->pages < 1) L->pages = 1;
  /* Below status bar + title row (see SCREEN_REPORT_LIST draw). */
  const int listTop = 88;
  L->navH = 40;
  L->navY = h - 8 - L->navH;
  const int listBottom = L->navY - 12;
  int gap = 6;
  int usable = listBottom - listTop;
  L->rowH = (usable - 4 * gap) / 5;
  if (L->rowH < 22) L->rowH = 22;
  for (int i = 0; i < 5; i++) L->rowY[i] = listTop + i * (L->rowH + gap);
  L->backX = 20;
  L->backY = L->navY;
  L->backW = 88;
  L->backH = L->navH;
  /* Delete + checkbox column only in field mode; training is view-only on device. */
  if (fieldMode && s_reportListCount > 0) {
    L->delW = 108;
    L->delX = w - 20 - L->delW;
    L->delY = L->navY;
    L->delH = L->navH;
  } else {
    L->delX = 0;
    L->delY = 0;
    L->delW = 0;
    L->delH = 0;
  }
  int innerL = L->backX + L->backW + 6;
  int innerR = (L->delW > 0) ? (L->delX - 6) : (w - 20);
  int innerW = innerR - innerL;
  L->showPager = (L->pages > 1 && innerW >= 120);
  const int gapN = 6;
  if (L->showPager) {
    int cell = (innerW - 2 * gapN) / 3;
    if (cell < 40) {
      L->showPager = false;
      L->midX = innerL;
      L->midW = innerW;
      L->prevX = L->prevW = L->nextX = L->nextW = 0;
    } else {
      int total = 3 * cell + 2 * gapN;
      int startX = innerL + (innerW - total) / 2;
      L->prevX = startX;
      L->prevW = cell;
      L->midX = startX + cell + gapN;
      L->midW = cell;
      L->nextX = startX + 2 * (cell + gapN);
      L->nextW = cell;
    }
  } else {
    L->midX = innerL;
    L->midW = innerW;
    L->prevX = L->prevW = L->nextX = L->nextW = 0;
  }
}

static void buildReportViewLineIndex(char* buf) {
  s_reportViewLineCount = 0;
  if (!buf || !buf[0]) return;
  char* p = buf;
  while (s_reportViewLineCount < 256) {
    s_reportViewLineOff[s_reportViewLineCount++] = (int)(p - buf);
    char* eol = strpbrk(p, "\r\n");
    if (!eol) break;
    *eol = '\0';
    p = eol + 1;
    while (*p == '\r' || *p == '\n') p++;
    if (*p == '\0') break;
  }
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

/** Begin verification for test index `rowIndex`. */
static ScreenId sparkyCommitTestSelectRow(int rowIndex) {
  const int n = VerificationSteps_getActiveTestCount();
  if (rowIndex < 0 || rowIndex >= n) return SCREEN_TEST_SELECT;
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

/* Clock after battery + Wi‑Fi; erase only the clock strip (not full width — that clipped titles). */
static const int kStatusBattLeftX = 10;
static const int kStatusWifiCx = 54;
static const int kStatusTimeX = 92;
static const int kStatusClockClearW = 104;

static void drawStatusBarTime(SparkyTft* tft) {
  char tb[20];
  SparkyTime_formatStatusBar(tb, sizeof(tb));
  tft->fillRect(kStatusTimeX - 2, 8, kStatusClockClearW, 14, kBg);
  tft->setTextWrap(false);
  tft->setTextSize(1);
  tft->setTextColor(kWhite, kBg);
  tft->setCursor(kStatusTimeX, 10);
  tft->print(tb);
}

/* Small "connected Wi‑Fi" glyph beside battery (white, same as battery outline). */
static void drawWifiConnectedIcon(SparkyTft* tft) {
  if (!WifiManager_isConnected() && !AdminPortal_isApActive()) return;
  const int cx = kStatusWifiCx;
  const int cy = 16;
  const int radii[3] = { 4, 7, 10 };
  for (int i = 0; i < 3; i++) {
    int r = radii[i];
    tft->drawCircle(cx, cy, r, kWhite);
    /* Keep only the upper arc to resemble Wi‑Fi waves. */
    tft->fillRect(cx - r - 1, cy, 2 * r + 2, r + 2, kBg);
  }
  tft->fillCircle(cx, cy + 4, 2, kWhite);
}

static void drawBatteryStatusIcon(SparkyTft* tft) {
  const int x = kStatusBattLeftX;
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
  if (event && strcmp(event, "session_saved") == 0)
    GoogleSync_queueResult(&gs);
  else
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
      /* Only clear the value column — full width erase was wiping the Del button. */
      {
        int clearW = rel.delX - 28 - 18;
        if (clearW < 80) clearW = rel.delX - 22 - 18;
        if (clearW < 40) clearW = 40;
        tft->fillRect(18, rel.valueRowY - 2, clearW, 52, kBg);
      }
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
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
      sparkyDisplayFlush(tft);
      return true;
    }
    default:
      return false;
  }
}

void Screens_drawStatusBar(SparkyTft* tft) {
  if (!tft) return;
  drawBatteryStatusIcon(tft);
  drawWifiConnectedIcon(tft);
  drawStatusBarTime(tft);
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
        tft->setCursor((w - tw) / 2, kScreenTitleYCenterSize2);
        tft->print(title);
      }
      tft->setTextSize(1);
      tft->setTextColor(kAccent, kBg);
      {
        const char* mode = AppState_isFieldMode() ? "Field mode" : "Training mode";
        int twm = (int)strlen(mode) * 6;
        tft->setCursor((w - twm) / 2, kScreenTitleYCenterSize2 + 20);
        tft->print(mode);
      }
      if (OtaUpdate_isInstallOfferPending()) {
        const int bannerY = kScreenTitleYCenterSize2 + 28;
        const int bannerH = readable ? 46 : 40;
        tft->fillRoundRect(20, bannerY, w - 40, bannerH, 8, 0xFD20);
        tft->drawRoundRect(20, bannerY, w - 40, bannerH, 8, kWhite);
        tft->setTextSize(1);
        tft->setTextColor(TFT_BLACK, 0xFD20);
        char pv[OTA_VERSION_LEN];
        OtaUpdate_getPendingVersion(pv, sizeof(pv));
        char line[56];
        if (pv[0])
          snprintf(line, sizeof(line), "Firmware v%s available — tap for Yes/No", pv);
        else
          strncpy(line, "Firmware update — tap here", sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
        tft->setTextWrap(false);
        int twl = (int)strlen(line) * 6;
        if (twl > w - 48) {
          strncpy(line, "Update ready — tap here", sizeof(line) - 1);
          line[sizeof(line) - 1] = '\0';
        }
        twl = (int)strlen(line) * 6;
        tft->setCursor((w - twl) / 2, bannerY + (readable ? 16 : 13));
        tft->print(line);
        y0 += bannerH + (readable ? 10 : 8);
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
        tft->setCursor((w - tw) / 2, kScreenTitleYCenterSize2);
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
        const int scopeY = panelWide ? 56 : (h >= 700 ? 58 : 50);
        sparkyDrawVerificationScope(tft, w, scopeY, 2);
      }
      sparkyDrawTestSelectPagedList(tft, w, h);
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
        sparkyDrawBtnLabel(tft, 20, btnY, w - 40, 48, "Save & end session", 2);
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
          const int clockReserve = 112;
          const int leftReserve = 24;
          int tw = (int)strlen(step.title) * 6 * titleTs;
          int cx = (w - tw) / 2;
          int maxRight = w - clockReserve;
          if (cx + tw > maxRight) cx = maxRight - tw;
          if (cx < leftReserve) cx = leftReserve;
          tft->setCursor(cx, 14);
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
      const int okW = 160;
      const int okH = 56;
      const int okY = h - 68;
      const int okX = w / 2 - okW / 2;
      tft->setTextColor(kGreen, kBg);
      tft->setTextSize(3);
      tft->setCursor(20, 28);
      tft->print("Report saved");
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setCursor(20, 64);
      tft->print(s_reportSavedBasename);
      tft->setCursor(20, 92);
        tft->print(".csv + .html saved - View reports from menu.");
      tft->fillRoundRect(okX, okY, okW, okH, 8, kBtn);
      tft->drawRoundRect(okX, okY, okW, okH, 8, kWhite);
      tft->setTextColor(kWhite, kBtn);
      sparkyDrawBtnLabel(tft, okX, okY, okW, okH, "OK", 3);
      break;
    }
    case SCREEN_REPORT_LIST: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      /* Title below status bar (battery/Wi‑Fi ~0–64) so it does not collide. */
      tft->setCursor(20, 32);
      tft->print("Reports");
      sparkyRefreshReportListCache();
      const bool fieldReports = AppState_isFieldMode();
      ReportListLayout rl;
      sparkyReportListComputeLayout(w, h, fieldReports, &rl);
      tft->setTextSize(1);
      tft->setTextColor((uint16_t)0xBDF7, kBg);
      tft->setCursor(20, 56);
      tft->print(fieldReports ? "Tap name to view  |  box = delete" : "Tap a report to view (CSV)");
      if (s_reportListCount == 0) {
        tft->setTextSize(1);
        tft->setTextColor(kWhite, kBg);
        tft->setCursor(20, 74);
        tft->print("(No reports yet)");
      } else {
        int first = s_reportListPage * kReportListPerPage;
        const uint8_t rowTs = sparkyReadableUi(w, h) ? (uint8_t)2 : (uint8_t)1;
        const int rowMainX = fieldReports ? (20 + kReportCheckCol) : 20;
        const int rowMainW = fieldReports ? (w - 40 - kReportCheckCol) : (w - 40);
        for (int slot = 0; slot < kReportListPerPage; slot++) {
          int idx = first + slot;
          int ry = rl.rowY[slot];
          int bh = rl.rowH - 2;
          if (idx >= s_reportListCount) {
            tft->fillRoundRect(20, ry, w - 40, bh, 6, kBg);
            tft->drawRoundRect(20, ry, w - 40, bh, 6, (uint16_t)0x4A69);
          } else {
            tft->fillRoundRect(rowMainX, ry, rowMainW, bh, 6, kBtn);
            tft->drawRoundRect(rowMainX, ry, rowMainW, bh, 6, kWhite);
            tft->setTextColor(kWhite, kBtn);
            sparkyDrawBtnLabel(tft, rowMainX, ry, rowMainW, bh, s_reportListNames[idx], rowTs);
            if (fieldReports) {
              const int cs = 20;
              const int cx = 24;
              const int cy = ry + (bh - cs) / 2;
              tft->drawRect(cx, cy, cs, cs, kWhite);
              if (s_reportDeleteMask & (1u << (unsigned)idx)) tft->fillRect(cx + 4, cy + 4, cs - 8, cs - 8, kGreen);
            }
          }
        }
      }
      bool prevEn = (s_reportListPage > 0);
      bool nextEn = (s_reportListPage < rl.pages - 1);
      uint16_t prevBg = prevEn ? kBtn : (uint16_t)0x18C3;
      uint16_t nextBg = nextEn ? kBtn : (uint16_t)0x18C3;
      if (rl.showPager) {
        tft->fillRoundRect(rl.prevX, rl.navY, rl.prevW, rl.navH, 6, prevBg);
        tft->drawRoundRect(rl.prevX, rl.navY, rl.prevW, rl.navH, 6, kWhite);
        tft->setTextColor(kWhite, prevBg);
        sparkyDrawBtnLabel(tft, rl.prevX, rl.navY, rl.prevW, rl.navH, "PREV", 2);
        tft->fillRect(rl.midX, rl.navY, rl.midW, rl.navH, kBg);
        char pg[12];
        snprintf(pg, sizeof(pg), "%d/%d", s_reportListPage + 1, rl.pages);
        tft->setTextSize(2);
        tft->setTextColor(kWhite, kBg);
        int ptw = (int)strlen(pg) * 12;
        tft->setCursor(rl.midX + (rl.midW - ptw) / 2, rl.navY + (rl.navH - 14) / 2);
        tft->print(pg);
        tft->fillRoundRect(rl.nextX, rl.navY, rl.nextW, rl.navH, 6, nextBg);
        tft->drawRoundRect(rl.nextX, rl.navY, rl.nextW, rl.navH, 6, kWhite);
        tft->setTextColor(kWhite, nextBg);
        sparkyDrawBtnLabel(tft, rl.nextX, rl.navY, rl.nextW, rl.navH, "NEXT", 2);
      }
      tft->fillRoundRect(rl.backX, rl.backY, rl.backW, rl.backH, 6, kBtn);
      tft->drawRoundRect(rl.backX, rl.backY, rl.backW, rl.backH, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      sparkyDrawBtnLabel(tft, rl.backX, rl.backY, rl.backW, rl.backH, "Back", 2);
      if (rl.delW > 0) {
        bool canDel = (s_reportDeleteMask != 0);
        uint16_t delBg = canDel ? kAccent : (uint16_t)0x3186;
        tft->fillRoundRect(rl.delX, rl.delY, rl.delW, rl.delH, 6, delBg);
        tft->drawRoundRect(rl.delX, rl.delY, rl.delW, rl.delH, 6, kWhite);
        tft->setTextColor(kWhite, delBg);
        sparkyDrawBtnLabel(tft, rl.delX, rl.delY, rl.delW, rl.delH, "Delete", 2);
      }
      break;
    }
    case SCREEN_REPORT_VIEW: {
      const int bodyTop = 62;
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      tft->setTextWrap(false);
      {
        char title[72];
        strncpy(title, s_reportViewBasename, sizeof(title) - 1);
        title[sizeof(title) - 1] = '\0';
        int maxCh = (w - 40) / 12;
        if (maxCh < 8) maxCh = 8;
        int tl = (int)strlen(title);
        if (tl > maxCh && maxCh > 3) {
          snprintf(title, sizeof(title), "%.*s...", maxCh - 3, s_reportViewBasename);
        }
        int tw = (int)strlen(title) * 12;
        tft->setCursor((w - tw) / 2, 28);
        tft->print(title);
      }
      const int navY = h - 44;
      const int lineStep = 18;
      tft->setTextSize(2);
      int maxVis = (navY - 8 - bodyTop) / lineStep;
      if (maxVis < 1) maxVis = 1;
      int maxScroll = s_reportViewLineCount - maxVis;
      if (maxScroll < 0) maxScroll = 0;
      if (s_reportViewScrollLine > maxScroll) s_reportViewScrollLine = maxScroll;
      tft->setTextColor(kWhite, kBg);
      if (s_reportViewLineCount <= 0) {
        tft->setCursor(20, bodyTop);
        tft->print("(Empty or unreadable report.)");
      } else {
        const int maxChars = (w - 44) / 12;
        for (int v = 0; v < maxVis; v++) {
          int li = s_reportViewScrollLine + v;
          if (li >= s_reportViewLineCount) break;
          const char* ln = s_reportViewCsv + s_reportViewLineOff[li];
          tft->setCursor(20, bodyTop + v * lineStep);
          tft->setTextWrap(false);
          size_t len = strlen(ln);
          if ((int)len > maxChars && maxChars > 3) {
            char cut[96];
            snprintf(cut, sizeof(cut), "%.*s...", maxChars - 3, ln);
            tft->print(cut);
          } else
            tft->print(ln);
        }
      }
      if (maxScroll > 0) {
        tft->setTextSize(1);
        tft->setTextColor((uint16_t)0xBDF7, kBg);
        tft->setCursor(20, navY - 14);
        tft->print("More below - use Down");
      }
      tft->fillRoundRect(20, navY, 88, 36, 6, kBtn);
      tft->drawRoundRect(20, navY, 88, 36, 6, kWhite);
      tft->setTextColor(kWhite, kBtn);
      tft->setTextSize(2);
      sparkyDrawBtnLabel(tft, 20, navY, 88, 36, "Back", 2);
      if (s_reportViewLineCount > maxVis) {
        bool upEn = (s_reportViewScrollLine > 0);
        bool dnEn = (s_reportViewScrollLine < maxScroll);
        int upX = w / 2 - 84;
        int dnX = w / 2 + 4;
        int sbW = 76;
        tft->fillRoundRect(upX, navY, sbW, 36, 6, upEn ? kBtn : (uint16_t)0x18C3);
        tft->drawRoundRect(upX, navY, sbW, 36, 6, kWhite);
        tft->setTextColor(kWhite, upEn ? kBtn : (uint16_t)0x18C3);
        sparkyDrawBtnLabel(tft, upX, navY, sbW, 36, "Up", 2);
        tft->fillRoundRect(dnX, navY, sbW, 36, 6, dnEn ? kBtn : (uint16_t)0x18C3);
        tft->drawRoundRect(dnX, navY, sbW, 36, 6, kWhite);
        tft->setTextColor(kWhite, dnEn ? kBtn : (uint16_t)0x18C3);
        sparkyDrawBtnLabel(tft, dnX, navY, sbW, 36, "Down", 2);
      }
      break;
    }
    case SCREEN_SETTINGS: {
      bool panelWide = (w >= 700);
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      {
        const char* t = "Settings";
        int tw = (int)strlen(t) * 6 * 2;
        tft->setCursor((w - tw) / 2, kScreenTitleYCenterSize2);
        tft->print(t);
      }
      {
        int backW = panelWide ? 96 : 92;
        int backH = 36;
        int backX = w - backW - 12;
        int backY = panelWide ? 10 : 8;
        tft->fillRoundRect(backX, backY, backW, backH, 6, kBtn);
        tft->drawRoundRect(backX, backY, backW, backH, 6, kWhite);
        tft->setTextSize(2);
        tft->setTextColor(kWhite, kBtn);
        int tx = 0, ty = 0;
        sparkyBackLabelCoords(backW, backH, backX, backY, &tx, &ty);
        tft->setCursor(tx, ty);
        tft->print("Back");
      }
      sparkyDrawSettingsPagedList(tft, w, h);
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
        tft->setCursor((w - tw) / 2, kScreenTitleYCenterSize2);
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
    case SCREEN_DATE_TIME: {
      SparkyRtc_refreshPresence();
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      {
        const char* t = "Date & time";
        int tw = (int)strlen(t) * 6 * 2;
        tft->setCursor((w - tw) / 2, kScreenTitleYCenterSize2);
        tft->print(t);
      }
      char ts[48];
      SparkyTime_formatPreferred(ts, sizeof(ts));
      tft->setTextSize(2);
      tft->setTextColor(kAccent, kBg);
      tft->setCursor(20, 36);
      tft->print(ts);
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(1);
      tft->setCursor(20, 58);
      tft->print(SparkyRtc_isPresent() ? "RTC: detected (PCF85063 / I2C)" : "RTC: not detected (re-open to re-scan)");
      const int btnW = w - 40;
      const int btnH = 48;
      const int rowStep = 58;
      int y = 78;
      char line[96];
      snprintf(line, sizeof(line), "12-hour display: %s", AppState_getClock12Hour() ? "On" : "Off");
      tft->fillRoundRect(20, y, btnW, btnH, 8, kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(kWhite, kBtn);
      sparkyDrawBtnLabel(tft, 20, y, btnW, btnH, line, 2);
      y += rowStep;
      tft->fillRoundRect(20, y, btnW, btnH, 8, kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      sparkyDrawBtnLabel(tft, 20, y, btnW, btnH, "Set device from RTC chip", 2);
      y += rowStep;
      tft->fillRoundRect(20, y, btnW, btnH, 8, kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      sparkyDrawBtnLabel(tft, 20, y, btnW, btnH, "Set date & time (dd/mm/yyyy)", 2);
      y += rowStep;
      tft->fillRoundRect(20, y, btnW, btnH, 8, kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      sparkyDrawBtnLabel(tft, 20, y, btnW, btnH, "Write clock to RTC chip", 2);
      y += rowStep;
      snprintf(line, sizeof(line), "NTP on WiFi: %s", AppState_getNtpEnabled() ? "On" : "Off");
      tft->fillRoundRect(20, y, btnW, btnH, 8, kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(kWhite, kBtn);
      sparkyDrawBtnLabel(tft, 20, y, btnW, btnH, line, 2);
      y += rowStep;
      {
        unsigned tzi = SparkyTzPresets_indexForOffset(AppState_getClockTzOffsetMinutes());
        char utcB[20];
        if (tzi < SparkyTzPresets_count()) {
          const SparkyTzPreset* tp = SparkyTzPresets_get(tzi);
          if (tp) {
            SparkyTzPresets_formatUtcOffset(tp->offsetMinutes, utcB, sizeof(utcB));
            snprintf(line, sizeof(line), "%s %s", utcB, tp->label);
          } else {
            strncpy(line, "Timezone: ?", sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
          }
        } else {
          SparkyTzPresets_formatUtcOffset(AppState_getClockTzOffsetMinutes(), utcB, sizeof(utcB));
          snprintf(line, sizeof(line), "Custom %s", utcB);
        }
      }
      tft->fillRoundRect(20, y, btnW, btnH, 8, kBtn);
      tft->drawRoundRect(20, y, btnW, btnH, 8, kWhite);
      tft->setTextColor(kWhite, kBtn);
      sparkyDrawBtnLabel(tft, 20, y, btnW, btnH, line, 1);
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
    case SCREEN_CLOCK_SET: {
      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      {
        const char* t = "Set date & time";
        int tw = (int)strlen(t) * 6 * 2;
        tft->setCursor((w - tw) / 2, kScreenTitleYCenterSize2);
        tft->print(t);
      }
      tft->setTextSize(1);
      tft->setTextColor(kAccent, kBg);
      char st[40];
      snprintf(st, sizeof(st), "Step %d of 6 - %s", s_clockWizardStep + 1, clockWizardFieldTitle(s_clockWizardStep));
      tft->setCursor(20, 38);
      tft->print(st);
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(20, 54);
      tft->print(s_clockWizInputLen > 0 ? s_clockWizInput : "(tap digits, Next to continue)");
      PinKeypadLayout pk;
      layoutNumericKeypad3x4(w, h, 88, &pk);
      const char* keys = "123456789D0E";
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
          if (keys[idx] == 'D')
            sparkyDrawBtnLabel(tft, kx, ky, pk.cellW, pk.cellH, "Del", pk.keypadTs);
          else if (keys[idx] == 'E')
            sparkyDrawBtnLabel(tft, kx, ky, pk.cellW, pk.cellH, "Next", pk.keypadTs);
          else {
            char ds[2] = { keys[idx], '\0' };
            sparkyDrawBtnLabel(tft, kx, ky, pk.cellW, pk.cellH, ds, pk.keypadTs);
          }
        }
      }
      {
        const bool panelWide = (w >= 700);
        const int backW = panelWide ? 96 : 92;
        const int backH = 36;
        const int backX = w - backW - 12;
        const int backY = panelWide ? 10 : 8;
        tft->fillRoundRect(backX, backY, backW, backH, 6, kBtn);
        tft->drawRoundRect(backX, backY, backW, backH, 6, kWhite);
        tft->setTextSize(2);
        tft->setTextColor(kWhite, kBtn);
        int tx = 0, ty = 0;
        sparkyBackLabelCoords(backW, backH, backX, backY, &tx, &ty);
        tft->setCursor(tx, ty);
        tft->print("Back");
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
        tft->setCursor((w - tw) / 2, kScreenTitleYCenterSize2);
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
      static const char kAboutBlurb[] =
          "SparkyCheck is a portable verification coach for electrical apprentices and electricians. "
          "It walks through AS/NZS-aligned checks, safety reminders, pass/fail capture, and saved "
          "reports you can review on the device or send by email when configured.";
      const bool readable = sparkyReadableUi(w, h);
      const uint8_t bodyTs = readable ? (uint8_t)2 : (uint8_t)1;
      const int lineH = 7 * (int)bodyTs + (readable ? 6 : 4);
      const int backH = readable ? 44 : 36;
      const int backY = h - backH - (readable ? 8 : 6);
      const int btnW = w - 40;
      const int yMax = backY - 8;
      const int marginX = 20;
      const int textW = w - 2 * marginX;

      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      {
        const char* title = "About SparkyCheck";
        int tw = (int)strlen(title) * 6 * 2;
        tft->setCursor((w - tw) / 2, kScreenTitleYCenterSize2);
        tft->print(title);
      }
      int y = kScreenTitleYCenterSize2 + (readable ? 22 : 18);
      tft->setTextSize(bodyTs);
      tft->setTextWrap(false);

      if (y + lineH <= yMax) {
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(marginX, y);
        tft->print("Firmware version");
        y += lineH;
      }
      if (y + lineH <= yMax) {
        tft->setTextColor(kWhite, kBg);
        tft->setCursor(marginX + 4, y);
        tft->print(OtaUpdate_getCurrentVersion());
        y += lineH;
      }

      int activeStd = 0;
      for (int sid = 0; sid < STANDARD_COUNT; sid++) {
        if (Standards_isActiveInCurrentMode((StandardId)sid)) activeStd++;
      }
      /* Leave room below blurb for credits + standards (wrapped) + rules; cap so short panels still get a blurb. */
      const int minTailLines = 6;
      const int perStdLines = 2;
      int tailLines = minTailLines + activeStd * perStdLines;
      if (tailLines > 14) tailLines = 14;
      int descMaxY = yMax - tailLines * lineH;
      if (descMaxY < y + 2 * lineH) descMaxY = y + 2 * lineH;
      if (y + lineH <= yMax) {
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(marginX, y);
        tft->print("What it is");
        y += lineH;
      }
      if (y < descMaxY) {
        tft->setTextColor(kWhite, kBg);
        y = sparkyDrawWrappedLeftUntilY(tft, kAboutBlurb, marginX, y, textW, descMaxY, bodyTs, lineH, kWhite, kBg);
        y += (readable ? 8 : 4);
        if (y > yMax) y = yMax;
      }
      if (y + lineH <= yMax) {
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(marginX, y);
        tft->print("Created by");
        y += lineH;
      }
      if (y + lineH <= yMax) {
        tft->setTextColor(kWhite, kBg);
        tft->setCursor(marginX, y);
        tft->print("Frank Offer 2026");
        y += lineH;
      }
      if (y + lineH <= yMax) {
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(marginX, y);
        tft->print("Current standards");
        y += lineH;
      }
      tft->setTextColor(kWhite, kBg);
      StandardInfo info;
      char stdLine[192];
      for (int sid = 0; sid < STANDARD_COUNT; sid++) {
        if (y >= yMax) break;
        Standards_getInfo((StandardId)sid, &info);
        if (!Standards_isActiveInCurrentMode((StandardId)sid)) continue;
        if (info.section && info.section[0])
          snprintf(stdLine, sizeof(stdLine), "%s %s", info.short_name, info.section);
        else
          snprintf(stdLine, sizeof(stdLine), "%s - %s", info.short_name, info.title);
        y = sparkyDrawWrappedLeftUntilY(tft, stdLine, marginX, y, textW, yMax, bodyTs, lineH, kWhite, kBg);
        y += (readable ? 2 : 1);
      }
      if (y + lineH <= yMax) {
        char rv[16];
        Standards_getRulesVersion(rv, sizeof(rv));
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(marginX, y);
        tft->print("Rules version: ");
        tft->setTextColor(kWhite, kBg);
        tft->print(rv);
      }

      tft->fillRoundRect(20, backY, btnW, backH, 6, kBtn);
      tft->drawRoundRect(20, backY, btnW, backH, 6, kWhite);
      sparkyDrawBtnLabel(tft, 20, backY, btnW, backH, "Back", 2);
      break;
    }
    case SCREEN_UPDATES: {
      const bool readable = sparkyReadableUi(w, h);
      const uint8_t bodyTs = readable ? (uint8_t)2 : (uint8_t)1;
      const uint8_t btnLblTs = readable ? (uint8_t)2 : (uint8_t)1;
      const int lineH = 7 * (int)bodyTs + (readable ? 4 : 3);
      const bool showOffer = OtaUpdate_isInstallOfferPending();
      const bool showStandaloneInstall = OtaUpdate_hasPendingUpdate() && !showOffer;
      int checkY = 0, offerY = -1, installY = 0, toggleY = 0, btnH = 0, gap = 0, half = 0, backY = 0, backH = 0;
      sparkyOtaUpdatesComputeLayout(w, h, readable, showOffer, showStandaloneInstall, &checkY, &offerY, &installY,
                                    &toggleY, &btnH, &gap, &half, &backY, &backH);
      const int contentMaxY = checkY - (readable ? 10 : 6);

      tft->setTextColor(kWhite, kBg);
      tft->setTextSize(2);
      {
        const char* title = "Firmware updates";
        int tw = (int)strlen(title) * 6 * 2;
        tft->setCursor((w - tw) / 2, kScreenTitleYCenterSize2);
        tft->print(title);
      }

      int y = kScreenTitleYCenterSize2 + (readable ? 22 : 18);
      tft->setTextSize(bodyTs);
      tft->setTextWrap(false);

      if (y + lineH <= contentMaxY) {
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(20, y);
        tft->print("Current firmware:");
        y += lineH;
      }
      if (y + lineH <= contentMaxY) {
        tft->setTextColor(kWhite, kBg);
        tft->setCursor(24, y);
        tft->print(OtaUpdate_getCurrentVersion());
        y += lineH;
      }
      if (y + lineH <= contentMaxY) {
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(20, y);
        tft->print("Auto-check after boot:");
        tft->setTextColor(kWhite, kBg);
        tft->setCursor(20 + (int)strlen("Auto-check after boot:") * 6 * bodyTs + 2 * (int)bodyTs, y);
        tft->print(AppState_getOtaAutoCheckEnabled() ? "On" : "Off");
        y += lineH;
      }

      char status[OTA_STATUS_LEN];
      OtaUpdate_getLastStatus(status, sizeof(status));
      char pending[OTA_VERSION_LEN];
      OtaUpdate_getPendingVersion(pending, sizeof(pending));
      if (y + lineH <= contentMaxY) {
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(20, y);
        tft->print("Status:");
        y += lineH;
      }
      {
        const int statusMaxY = contentMaxY - (pending[0] ? lineH : 0);
        if (y < statusMaxY) {
          tft->setTextColor(kWhite, kBg);
          y = sparkyDrawWrappedLeftUntilY(tft, status, 20, y, w - 40, statusMaxY, bodyTs, lineH, kWhite, kBg);
          y += (readable ? 4 : 2);
          if (y > contentMaxY) y = contentMaxY;
        }
      }

      if (pending[0] && y + lineH <= contentMaxY) {
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(20, y);
        tft->print("Pending:");
        tft->setTextColor(kWhite, kBg);
        tft->setCursor(20 + (int)strlen("Pending:") * 6 * bodyTs + 4 * (int)bodyTs, y);
        tft->print(pending);
      }

      int btnY = checkY;
      tft->fillRoundRect(20, btnY, w - 40, btnH, 6, kGreen);
      tft->drawRoundRect(20, btnY, w - 40, btnH, 6, kWhite);
      sparkyDrawBtnLabel(tft, 20, btnY, w - 40, btnH, "Check now", btnLblTs);

      if (showOffer && offerY >= 0) {
        btnY = offerY;
        if (btnY > kScreenTitleYCenterSize2 + 8) {
          tft->setTextSize(1);
          tft->setTextColor(kAccent, kBg);
          tft->setCursor(20, btnY - (readable ? 22 : 18));
          tft->print("Tap Install twice (flicker OK) or dismiss:");
        }
        tft->fillRoundRect(20, btnY, half, btnH, 6, kGreen);
        tft->drawRoundRect(20, btnY, half, btnH, 6, kWhite);
        sparkyDrawBtnLabel(tft, 20, btnY, half, btnH, "Install", btnLblTs);
        tft->fillRoundRect(30 + half, btnY, half, btnH, 6, kBtn);
        tft->drawRoundRect(30 + half, btnY, half, btnH, 6, kWhite);
        sparkyDrawBtnLabel(tft, 30 + half, btnY, half, btnH, "Not now", btnLblTs);
      }

      if (installY >= 0) {
        btnY = installY;
        tft->setTextSize(1);
        tft->setTextColor(kAccent, kBg);
        tft->setCursor(20, btnY - (readable ? 14 : 12));
        tft->print("Tap Install twice; flicker OK, then reboot");
        tft->fillRoundRect(20, btnY, w - 40, btnH, 6, kAccent);
        tft->drawRoundRect(20, btnY, w - 40, btnH, 6, kWhite);
        sparkyDrawBtnLabel(tft, 20, btnY, w - 40, btnH, "Install update", btnLblTs);
      }

      btnY = toggleY;
      tft->fillRoundRect(20, btnY, w - 40, btnH, 6, kBtn);
      tft->drawRoundRect(20, btnY, w - 40, btnH, 6, kWhite);
      sparkyDrawBtnLabel(tft, 20, btnY, w - 40, btnH, "Toggle auto-check", btnLblTs);

      tft->fillRoundRect(20, backY, w - 40, backH, 6, kBtn);
      tft->drawRoundRect(20, backY, w - 40, backH, 6, kWhite);
      sparkyDrawBtnLabel(tft, 20, backY, w - 40, backH, "Back", 2);
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
  Screens_drawStatusBar(tft);
  sparkyDisplayFlush(tft);
}

static bool statusBarLeftPeriodicRefreshOk(ScreenId id) {
  switch (id) {
    case SCREEN_STUDENT_ID:
    case SCREEN_TEST_FLOW:
    case SCREEN_REPORT_SAVED:
    case SCREEN_REPORT_LIST:
    case SCREEN_REPORT_VIEW:
    case SCREEN_PIN_ENTER:
    case SCREEN_CHANGE_PIN:
    case SCREEN_EMAIL_FIELD_EDIT:
    case SCREEN_WIFI_PASSWORD:
    case SCREEN_TRAINING_SYNC_EDIT:
    case SCREEN_DATE_TIME:
    case SCREEN_CLOCK_SET:
      return false;
    default:
      return true;
  }
}

void Screens_refreshLiveStatus(SparkyTft* tft, ScreenId currentScreen) {
  if (!tft) return;
  static unsigned long s_lastClockPollMs = 0;
  static unsigned long s_lastLeftPollMs = 0;
  static char s_prevClock[16] = "";

  unsigned long now = millis();
  if (now - s_lastClockPollMs >= 1000) {
    s_lastClockPollMs = now;
    char tb[20];
    SparkyTime_formatStatusBar(tb, sizeof(tb));
    if (strcmp(tb, s_prevClock) != 0) {
      strncpy(s_prevClock, tb, sizeof(s_prevClock) - 1);
      s_prevClock[sizeof(s_prevClock) - 1] = '\0';
      tft->fillRect(kStatusTimeX - 2, 8, kStatusClockClearW, 14, kBg);
      tft->setTextWrap(false);
      tft->setTextSize(1);
      tft->setTextColor(kWhite, kBg);
      tft->setCursor(kStatusTimeX, 10);
      tft->print(tb);
      sparkyDisplayFlush(tft);
    }
  }

  if (now - s_lastLeftPollMs >= 10000 && statusBarLeftPeriodicRefreshOk(currentScreen)) {
    s_lastLeftPollMs = now;
    /* Battery + Wi‑Fi only; leave kStatusTimeX band for the clock tick handler. */
    tft->fillRect(4, 4, kStatusTimeX - 8, 22, kBg);
    drawBatteryStatusIcon(tft);
    drawWifiConnectedIcon(tft);
    sparkyDisplayFlush(tft);
  }
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
      const bool readableMm = sparkyReadableUi(w, h);
      if (OtaUpdate_isInstallOfferPending()) {
        const int bannerY = kScreenTitleYCenterSize2 + 28;
        const int bannerH = readableMm ? 46 : 40;
        if (inRect(ix, iy, 20, bannerY, w - 40, bannerH)) return handled(SCREEN_UPDATES);
        y0 += bannerH + (readableMm ? 10 : 8);
      }
      int y1 = y0 + btnH + gap;
      int y2 = y1 + btnH + gap;
      if (inRect(ix, iy, 20, y0, w - 40, btnH)) {
        s_testSelectPage = 0;
        return handled(SCREEN_TEST_SELECT);
      }
      if (inRect(ix, iy, 20, y1, w - 40, btnH)) {
        s_reportListPage = 0;
        s_reportDeleteMask = 0;
        return handled(SCREEN_REPORT_LIST);
      }
      if (inRect(ix, iy, 20, y2, w - 40, btnH)) {
        if (AppState_getMode() == APP_MODE_TRAINING && !s_trainingSettingsUnlocked) {
          Screens_resetPinEntry();
          Screens_setPinSuccessTarget(SCREEN_SETTINGS);
          Screens_setPinCancelTarget(SCREEN_MAIN_MENU);
          return handled(SCREEN_PIN_ENTER);
        }
        s_settingsPage = 0;
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
      if (inRect(ix, iy, backX, backY, backW, backH)) return handled(SCREEN_MAIN_MENU);
      const int testCount = VerificationSteps_getActiveTestCount();
      if (testCount <= 0) break;
      sparkyTestSelectClampPage(testCount);
      const int pages = sparkyTestSelectNumPages(testCount);
      SparkyTestSelectPagedLayout L;
      sparkyTestSelectComputePagedLayout(w, h, &L);
      if (inRect(ix, iy, L.prevX, L.navY, L.prevW, L.navH)) {
        if (s_testSelectPage > 0) {
          s_testSelectPage--;
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
          EezMockupUi_draw(tft, current);
#else
          Screens_draw(tft, current);
#endif
          return handled(current);
        }
        return current;
      }
      if (inRect(ix, iy, L.nextX, L.navY, L.nextW, L.navH)) {
        if (s_testSelectPage < pages - 1) {
          s_testSelectPage++;
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
          EezMockupUi_draw(tft, current);
#else
          Screens_draw(tft, current);
#endif
          return handled(current);
        }
        return current;
      }
      int first = s_testSelectPage * kTestSelectPerPage;
      for (int slot = 0; slot < kTestSelectPerPage; slot++) {
        int idx = first + slot;
        int y = L.rowY[slot];
        int bh = L.rowH - 2;
        if (!inRect(ix, iy, 20, y, w - 40, bh)) continue;
        if (idx >= testCount) return current;
        ScreenId next = sparkyCommitTestSelectRow(idx);
        if (next != current) s_handledButton = true;
        return next;
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
        s_testSelectPage = 0;
        return handled(SCREEN_TEST_SELECT);
      }
      if (s_flowPhase == 1) {
        if (inRect(ix, iy, 20, h - 56, w - 40, 48)) {
          ReportGenerator_setStudentId(AppState_getMode() == APP_MODE_TRAINING ? s_studentId : "");
          ReportGenerator_begin(nullptr, VerificationSteps_getTestName((VerifyTestId)s_selectedTestType));
          char valBuf[24];
          if (s_resultUnit && s_resultUnit[0])
            snprintf(valBuf, sizeof(valBuf), "%.3f", (double)s_resultValue);
          else
            snprintf(valBuf, sizeof(valBuf), "Verified");
          ReportGenerator_addResult(s_resultLabel ? s_resultLabel : "Test", valBuf, s_resultUnit ? s_resultUnit : "", s_resultPass, s_resultClause ? s_resultClause : "");
          ReportGenerator_end();
          char base[64];
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
          /* Partial redraw skips the Del control; full redraw keeps keypad/Del in sync. */
          Screens_draw(tft, current, true);
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
    case SCREEN_REPORT_SAVED: {
      const int okW = 160;
      const int okH = 56;
      const int okY = h - 68;
      const int okX = w / 2 - okW / 2;
      if (inRect(ix, iy, okX, okY, okW, okH)) {
        s_testSelectPage = 0;
        return handled(SCREEN_TEST_SELECT);
      }
      break;
    }
    case SCREEN_REPORT_LIST: {
      sparkyRefreshReportListCache();
      const bool fieldReports = AppState_isFieldMode();
      ReportListLayout rl;
      sparkyReportListComputeLayout(w, h, fieldReports, &rl);
      if (inRect(ix, iy, rl.backX, rl.backY, rl.backW, rl.backH)) {
        s_reportDeleteMask = 0;
        return handled(SCREEN_MAIN_MENU);
      }
      if (fieldReports && rl.delW > 0 && inRect(ix, iy, rl.delX, rl.delY, rl.delW, rl.delH)) {
        if (s_reportDeleteMask) {
          for (int i = 0; i < s_reportListCount; i++) {
            if (s_reportDeleteMask & (1u << (unsigned)i)) ReportGenerator_deleteReportBasename(s_reportListNames[i]);
          }
          s_reportDeleteMask = 0;
          sparkyRefreshReportListCache();
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
          EezMockupUi_draw(tft, current);
#else
          Screens_draw(tft, current);
#endif
          return handled(current);
        }
        return current;
      }
      if (rl.showPager) {
        if (inRect(ix, iy, rl.prevX, rl.navY, rl.prevW, rl.navH)) {
          if (s_reportListPage > 0) {
            s_reportListPage--;
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
            EezMockupUi_draw(tft, current);
#else
            Screens_draw(tft, current);
#endif
            return handled(current);
          }
          return current;
        }
        if (inRect(ix, iy, rl.nextX, rl.navY, rl.nextW, rl.navH)) {
          if (s_reportListPage < rl.pages - 1) {
            s_reportListPage++;
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
            EezMockupUi_draw(tft, current);
#else
            Screens_draw(tft, current);
#endif
            return handled(current);
          }
          return current;
        }
      }
      int first = s_reportListPage * kReportListPerPage;
      const int rowMainX = fieldReports ? (20 + kReportCheckCol) : 20;
      const int rowMainW = fieldReports ? (w - 40 - kReportCheckCol) : (w - 40);
      for (int slot = 0; slot < kReportListPerPage; slot++) {
        int idx = first + slot;
        if (idx >= s_reportListCount) break;
        int y = rl.rowY[slot];
        int bh = rl.rowH - 2;
        if (fieldReports) {
          if (inRect(ix, iy, 20, y, kReportCheckCol, bh)) {
            s_reportDeleteMask ^= (1u << (unsigned)idx);
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
            EezMockupUi_draw(tft, current);
#else
            Screens_draw(tft, current);
#endif
            return handled(current);
          }
        }
        if (inRect(ix, iy, rowMainX, y, rowMainW, bh)) {
          strncpy(s_reportViewBasename, s_reportListNames[idx], sizeof(s_reportViewBasename) - 1);
          s_reportViewBasename[sizeof(s_reportViewBasename) - 1] = '\0';
          if (!ReportGenerator_readReportCsv(s_reportViewBasename, s_reportViewCsv, (unsigned)sizeof(s_reportViewCsv), nullptr)) {
            strncpy(s_reportViewCsv, "(No CSV for this report.)", sizeof(s_reportViewCsv) - 1);
            s_reportViewCsv[sizeof(s_reportViewCsv) - 1] = '\0';
          }
          buildReportViewLineIndex(s_reportViewCsv);
          s_reportViewScrollLine = 0;
          return handled(SCREEN_REPORT_VIEW);
        }
      }
      break;
    }
    case SCREEN_REPORT_VIEW: {
      const int navY = h - 44;
      const int bodyTop = 62;
      const int lineStep = 18;
      int maxVis = (navY - 8 - bodyTop) / lineStep;
      if (maxVis < 1) maxVis = 1;
      int maxScroll = s_reportViewLineCount - maxVis;
      if (maxScroll < 0) maxScroll = 0;
      if (inRect(ix, iy, 20, navY, 88, 36)) return handled(SCREEN_REPORT_LIST);
      if (s_reportViewLineCount > maxVis) {
        int upX = w / 2 - 84;
        int dnX = w / 2 + 4;
        int sbW = 76;
        if (inRect(ix, iy, upX, navY, sbW, 36) && s_reportViewScrollLine > 0) {
          s_reportViewScrollLine--;
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
          EezMockupUi_draw(tft, current);
#else
          Screens_draw(tft, current);
#endif
          return handled(current);
        }
        if (inRect(ix, iy, dnX, navY, sbW, 36) && s_reportViewScrollLine < maxScroll) {
          s_reportViewScrollLine++;
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
          EezMockupUi_draw(tft, current);
#else
          Screens_draw(tft, current);
#endif
          return handled(current);
        }
      }
      break;
    }
    case SCREEN_SETTINGS: {
      if (AppState_getMode() == APP_MODE_TRAINING && !s_trainingSettingsUnlocked) {
        Screens_resetPinEntry();
        Screens_setPinSuccessTarget(SCREEN_SETTINGS);
        Screens_setPinCancelTarget(SCREEN_MAIN_MENU);
        return handled(SCREEN_PIN_ENTER);
      }
      bool field = AppState_isFieldMode();
      settingsClampPage(field);
      const int n = settingsRowCount(field);
      const int pages = settingsNumPages(field);
      bool panelWide = (w >= 700);
      int backW = panelWide ? 96 : 92;
      int backH = 36;
      int backX = w - backW - 12;
      int backY = panelWide ? 10 : 8;
      if (inRect(ix, iy, backX, backY, backW, backH)) {
        s_trainingSettingsUnlocked = false;
        return handled(SCREEN_MAIN_MENU);
      }
      SparkyTestSelectPagedLayout L;
      sparkyTestSelectComputePagedLayout(w, h, &L);
      if (inRect(ix, iy, L.prevX, L.navY, L.prevW, L.navH)) {
        if (s_settingsPage > 0) {
          s_settingsPage--;
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
          EezMockupUi_draw(tft, current);
#else
          Screens_draw(tft, current);
#endif
          return handled(current);
        }
        return current;
      }
      if (inRect(ix, iy, L.nextX, L.navY, L.nextW, L.navH)) {
        if (s_settingsPage < pages - 1) {
          s_settingsPage++;
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
          EezMockupUi_draw(tft, current);
#else
          Screens_draw(tft, current);
#endif
          return handled(current);
        }
        return current;
      }
      int first = s_settingsPage * kTestSelectPerPage;
      for (int slot = 0; slot < kTestSelectPerPage; slot++) {
        int idx = first + slot;
        int rowY = L.rowY[slot];
        int bh = L.rowH - 2;
        if (!inRect(ix, iy, 20, rowY, w - 40, bh)) continue;
        if (idx >= n) return current;
        ScreenId dest = settingsRowNavigate(tft, field, idx);
        if (dest == SCREEN_SETTINGS) {
#if defined(SPARKYCHECK_EEZ_MOCKUP_UI)
          EezMockupUi_draw(tft, current);
#else
          Screens_draw(tft, current, false);
#endif
          return handled(current);
        }
        return handled(dest);
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
    case SCREEN_DATE_TIME: {
      const int btnW = w - 40;
      const int btnH = 48;
      const int rowStep = 58;
      int y = 78;
      if (inRect(ix, iy, 20, y, btnW, btnH)) {
        AppState_setClock12Hour(!AppState_getClock12Hour());
        Screens_draw(tft, current);
        return handled(current);
      }
      y += rowStep;
      if (inRect(ix, iy, 20, y, btnW, btnH)) {
        if (SparkyRtc_syncSystemFromRtc()) showSavedPrompt(tft, "Time from RTC");
        else showSavedPrompt(tft, "RTC read failed");
        return handled(current);
      }
      y += rowStep;
      if (inRect(ix, iy, 20, y, btnW, btnH)) {
        clockWizardBegin();
        return handled(SCREEN_CLOCK_SET);
      }
      y += rowStep;
      if (inRect(ix, iy, 20, y, btnW, btnH)) {
        if (SparkyRtc_writeFromSystemClock()) showSavedPrompt(tft, "Saved to RTC");
        else showSavedPrompt(tft, "RTC write failed");
        return handled(current);
      }
      y += rowStep;
      if (inRect(ix, iy, 20, y, btnW, btnH)) {
        AppState_setNtpEnabled(!AppState_getNtpEnabled());
        SparkyNtp_requestRestart();
        Screens_draw(tft, current);
        showSavedPrompt(tft, AppState_getNtpEnabled() ? "NTP: On" : "NTP: Off");
        Screens_draw(tft, current);
        return handled(current);
      }
      y += rowStep;
      if (inRect(ix, iy, 20, y, btnW, btnH)) {
        sparkyCycleTimezonePreset();
        unsigned after = SparkyTzPresets_indexForOffset(AppState_getClockTzOffsetMinutes());
        const SparkyTzPreset* tp = SparkyTzPresets_get(after < SparkyTzPresets_count() ? after : 0);
        Screens_draw(tft, current);
        char msg[80];
        snprintf(msg, sizeof(msg), "TZ: %s", tp && tp->label ? tp->label : "set");
        showSavedPrompt(tft, msg);
        Screens_draw(tft, current);
        return handled(current);
      }
      if (inRect(ix, iy, 20, h - 52, btnW, 44)) return handled(SCREEN_SETTINGS);
      break;
    }
    case SCREEN_CLOCK_SET: {
      {
        const bool panelWide = (w >= 700);
        const int backW = panelWide ? 96 : 92;
        const int backH = 36;
        const int backX = w - backW - 12;
        const int backY = panelWide ? 10 : 8;
        if (inRect(ix, iy, backX, backY, backW, backH)) return handled(SCREEN_DATE_TIME);
      }
      PinKeypadLayout pk;
      layoutNumericKeypad3x4(w, h, 88, &pk);
      const char* keys = "123456789D0E";
      int maxDig = (s_clockWizardStep == 2) ? 4 : 2;
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
          int idx = row * 3 + col;
          int kx = pk.kx + col * (pk.cellW + pk.gap);
          int ky = pk.ky + row * (pk.cellH + pk.gap);
          if (!inRect(ix, iy, kx, ky, pk.cellW, pk.cellH)) continue;
          char k = keys[idx];
          if (k == 'D') {
            if (s_clockWizInputLen > 0) s_clockWizInput[--s_clockWizInputLen] = '\0';
            Screens_draw(tft, current, false);
            return handled(current);
          }
          if (k == 'E') {
            int v = 0;
            if (!clockWizardCommitStep(&v)) {
              Buzzer_beepFail();
              Screens_draw(tft, current, false);
              return handled(current);
            }
            switch (s_clockWizardStep) {
              case 0: s_clockWizDay = v; break;
              case 1: s_clockWizMon = v; break;
              case 2: s_clockWizYear = v; break;
              case 3: s_clockWizHour = v; break;
              case 4: s_clockWizMin = v; break;
              case 5: s_clockWizSec = v; break;
              default: break;
            }
            if (s_clockWizardStep >= 5) {
              if (SparkyTime_setLocalDateTime(s_clockWizDay, s_clockWizMon, s_clockWizYear, s_clockWizHour, s_clockWizMin,
                                              s_clockWizSec, true))
                showSavedPrompt(tft, "Clock updated");
              else
                showSavedPrompt(tft, "Invalid date/time");
              return handled(SCREEN_DATE_TIME);
            }
            s_clockWizardStep++;
            clockWizardPrefillInput();
            Screens_draw(tft, current);
            return handled(current);
          }
          if (k >= '0' && k <= '9' && s_clockWizInputLen < maxDig) {
            s_clockWizInput[s_clockWizInputLen++] = k;
            s_clockWizInput[s_clockWizInputLen] = '\0';
            Screens_draw(tft, current, false);
            return handled(current);
          }
          return handled(current);
        }
      }
      break;
    }
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
    case SCREEN_ABOUT: {
      const bool readable = sparkyReadableUi(w, h);
      const int backH = readable ? 44 : 36;
      const int backY = h - backH - (readable ? 8 : 6);
      if (inRect(ix, iy, 20, backY, w - 40, backH)) return handled(SCREEN_SETTINGS);
      break;
    }
    case SCREEN_UPDATES: {
      const bool readable = sparkyReadableUi(w, h);
      const bool showOffer = OtaUpdate_isInstallOfferPending();
      const bool showStandaloneInstall = OtaUpdate_hasPendingUpdate() && !showOffer;
      int checkY = 0, offerY = -1, installY = 0, toggleY = 0, btnH = 0, gap = 0, half = 0, backY = 0, backH = 0;
      sparkyOtaUpdatesComputeLayout(w, h, readable, showOffer, showStandaloneInstall, &checkY, &offerY, &installY,
                                    &toggleY, &btnH, &gap, &half, &backY, &backH);
      int btnY = checkY;
      if (inRect(ix, iy, 20, btnY, w - 40, btnH)) {
        sparkyOtaDisarmInstallConfirm();
        if (!OtaUpdate_isManifestCheckBusy()) OtaUpdate_startManifestCheckFromUi();
        Screens_draw(tft, current);
        return handled(current);
      }
      if (showOffer && offerY >= 0) {
        btnY = offerY;
        if (inRect(ix, iy, 20, btnY, half, btnH)) {
          if (OtaUpdate_hasPendingUpdate()) {
            if (!s_otaInstallConfirmArmed) {
              s_otaInstallConfirmArmed = true;
              showPrompt(tft, "Screen may flicker during install", "Normal. Tap Install again to start", kAccent);
              Screens_draw(tft, current);
              return handled(current);
            }
            sparkyOtaDisarmInstallConfirm();
            OtaUpdate_installPending();
          }
          Screens_draw(tft, current);
          return handled(current);
        }
        if (inRect(ix, iy, 30 + half, btnY, half, btnH)) {
          sparkyOtaDisarmInstallConfirm();
          OtaUpdate_dismissInstallOffer();
          Screens_draw(tft, current);
          showSavedPrompt(tft, "You can install later from here.");
          Screens_draw(tft, current);
          return handled(current);
        }
      }
      if (installY >= 0) {
        btnY = installY;
        if (inRect(ix, iy, 20, btnY, w - 40, btnH)) {
          if (OtaUpdate_hasPendingUpdate()) {
            if (!s_otaInstallConfirmArmed) {
              s_otaInstallConfirmArmed = true;
              showPrompt(tft, "Screen may flicker during install", "Normal. Tap Install again to start", kAccent);
              Screens_draw(tft, current);
              return handled(current);
            }
            sparkyOtaDisarmInstallConfirm();
            OtaUpdate_installPending();
          }
          Screens_draw(tft, current);
          return handled(current);
        }
      }
      btnY = toggleY;
      if (inRect(ix, iy, 20, btnY, w - 40, btnH)) {
        sparkyOtaDisarmInstallConfirm();
        bool enabled = !AppState_getOtaAutoCheckEnabled();
        AppState_setOtaAutoCheckEnabled(enabled);
        Screens_draw(tft, current);
        showSavedPrompt(tft, enabled ? "Auto-check: ON" : "Auto-check: OFF");
        Screens_draw(tft, current);
        return handled(current);
      }
      if (inRect(ix, iy, 20, backY, w - 40, backH)) {
        sparkyOtaDisarmInstallConfirm();
        if (OtaUpdate_isInstallOfferPending()) OtaUpdate_dismissInstallOffer();
        return handled(SCREEN_SETTINGS);
      }
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
              if (s_pinSuccessTarget == SCREEN_SETTINGS) {
                s_trainingSettingsUnlocked = true;
                s_settingsPage = 0;
              }
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
  (void)tft;
  (void)x;
  (void)y;
  s_handledButton = false;
  return current;
}

ScreenId Screens_handleTouchEnd(SparkyTft* tft, ScreenId current, uint16_t x, uint16_t y) {
  (void)tft;
  (void)x;
  (void)y;
  s_handledButton = false;
  return current;
}
