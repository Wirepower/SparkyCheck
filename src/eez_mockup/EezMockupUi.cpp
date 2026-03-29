#include "EezMockupUi.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "AppState.h"
#include "EezMockupData.h"
#include "Screens.h"
#include "Standards.h"
#include "OtaUpdate.h"

namespace {

/** Enough pixels in either dimension for comfortable text (landscape 800 or portrait 800). */
static bool readableUi(SparkyTft* tft) {
  if (!tft) return false;
  const int W = tft->width(), H = tft->height();
  return (W >= 700) || (H >= 700) || (W >= 480 && H >= 600);
}

/** Layout tuned for horizontal 800-class panels. */
static bool layoutLandWide(SparkyTft* tft) {
  return tft && tft->width() >= 700;
}

static void mainMenuButtonLayoutDims(int W, int H, int* y0, int* btnH, int* gap) {
  if (W >= 700) {
    *y0 = 88;
    *btnH = 64;
    *gap = 14;
  } else if (H >= 700) {
    *y0 = 100;
    *btnH = 56;
    *gap = 14;
  } else {
    *y0 = 80;
    *btnH = 48;
    *gap = 12;
  }
}

static void mainMenuButtonLayout(SparkyTft* tft, int* y0, int* btnH, int* gap) {
  mainMenuButtonLayoutDims(tft->width(), tft->height(), y0, btnH, gap);
}

/** Match Screens.cpp sparkySettingsLayout for touch/delegate parity. */
static void settingsLayoutDims(int W, int H, bool field, int* y0, int* btnH, int* gap, int* nRows, int* backY) {
  const bool r = (W >= 700) || (H >= 700) || (W >= 480 && H >= 600);
  *gap = 6;
  *btnH = r ? 38 : 32;
  *y0 = 56;
  *nRows = field ? 9 : 11;
  *backY = *y0 + *nRows * (*btnH + *gap) + 8;
  if (!field && *nRows >= 11 && H <= 500) {
    *gap = 4;
    if (r && *btnH > 32) *btnH = 34;
    *backY = *y0 + *nRows * (*btnH + *gap) + 8;
  }
}

/** Row index for Settings screen — matches Screens.cpp sparkySettingsLayout order. */
static int settingsRowFromLabelField(const char* lbl) {
  if (!lbl) return -1;
  if (strcmp(lbl, "Screen rotation") == 0) return 0;
  if (strcmp(lbl, "WiFi connection") == 0) return 1;
  if (strcmp(lbl, "Buzzer (sound)") == 0) return 2;
  if (strcmp(lbl, "Date & time") == 0) return 3;
  if (strcmp(lbl, "About") == 0) return 4;
  if (strcmp(lbl, "Firmware updates") == 0) return 5;
  if (strcmp(lbl, "Email settings") == 0) return 6;
  if (strcmp(lbl, "Mode change (boot hold)") == 0) return 7;
  if (strcmp(lbl, "Restart device") == 0) return 8;
  return -1;
}

static int settingsRowFromLabelTraining(const char* lbl) {
  if (!lbl) return -1;
  if (strcmp(lbl, "Screen rotation") == 0) return 0;
  if (strcmp(lbl, "WiFi connection") == 0) return 1;
  if (strcmp(lbl, "Buzzer (sound)") == 0) return 2;
  if (strcmp(lbl, "Date & time") == 0) return 3;
  if (strcmp(lbl, "About") == 0) return 4;
  if (strcmp(lbl, "Firmware updates") == 0) return 5;
  if (strcmp(lbl, "Training sync (PIN)") == 0) return 6;
  if (strcmp(lbl, "Email settings") == 0) return 7;
  if (strcmp(lbl, "Mode change (boot hold)") == 0) return 8;
  if (strcmp(lbl, "Restart device") == 0) return 9;
  if (strcmp(lbl, "Change PIN") == 0) return 10;
  return -1;
}

static void backButtonSizeDims(int W, int* bw, int* bh) {
  if (W >= 700) {
    *bw = 96;
    *bh = 36;
  } else {
    *bw = 92;
    *bh = 36;
  }
}

static void backButtonSize(SparkyTft* tft, int* bw, int* bh) {
  backButtonSizeDims(tft->width(), bw, bh);
}

static const uint16_t kBg = 0x18E3;
static const uint16_t kBtn = 0x2D6A;
static const uint16_t kAccent = 0xFD20;
static const uint16_t kWhite = 0xFFFF;
static const uint16_t kGreen = kBtn;
static const uint16_t kRed = 0xF800;

static bool s_handledButton = false;

static void toLowerInPlace(char* s) {
  if (!s) return;
  for (; *s; ++s) *s = (char)tolower((unsigned char)*s);
}

static bool strContainsLowered(const char* haystack, const char* needleLower) {
  if (!haystack || !needleLower || !needleLower[0]) return false;
  char buf[96];
  strncpy(buf, haystack, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  toLowerInPlace(buf);
  return strstr(buf, needleLower) != nullptr;
}

static bool isAccentButton(const char* text) {
  if (!text || !text[0]) return false;
  return strContainsLowered(text, "continue") ||
         strContainsLowered(text, "ok") ||
         strContainsLowered(text, "save") ||
         strContainsLowered(text, "connect") ||
         strContainsLowered(text, "start");
}

static bool isLegacyOnlyScreen(ScreenId id) {
  switch (id) {
    case SCREEN_STUDENT_ID:
    case SCREEN_TEST_FLOW:
    case SCREEN_REPORT_SAVED:
    case SCREEN_REPORT_LIST:
    case SCREEN_REPORT_VIEW:
    /* Real Wi-Fi list + scan lives in Screens.cpp; EEZ mockup would overwrite scan results. */
    case SCREEN_WIFI_LIST:
    case SCREEN_WIFI_PASSWORD:
    /* Full product copy + standards list (Screens.cpp); mockup only showed title + wide Back bar. */
    case SCREEN_ABOUT:
    /* Real OTA layout + hit targets (Screens.cpp); mockup stacked wrong buttons vs. legacy rects. */
    case SCREEN_UPDATES:
    case SCREEN_TRAINING_SYNC_EDIT:
    case SCREEN_EMAIL_FIELD_EDIT:
    case SCREEN_CHANGE_PIN:
    case SCREEN_PIN_ENTER:
    case SCREEN_DATE_TIME:
    case SCREEN_CLOCK_SET:
      return true;
    default:
      return false;
  }
}

static bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh);
}

/** Slightly expand hit target (helps finger alignment vs drawn compact rows). */
static bool inRectPad(int x, int y, int rx, int ry, int rw, int rh, int pad) {
  rx -= pad;
  ry -= pad;
  rw += 2 * pad;
  rh += 2 * pad;
  if (rx < 0) {
    rw += rx;
    rx = 0;
  }
  if (ry < 0) {
    rh += ry;
    ry = 0;
  }
  return inRect(x, y, rx, ry, rw, rh);
}

static int sx(SparkyTft* tft, int x) {
  if (!tft) return x;
  return (x * tft->width()) / 800;
}

static int sy(SparkyTft* tft, int y) {
  if (!tft) return y;
  return (y * tft->height()) / 480;
}

static int sw(SparkyTft* tft, int w) {
  if (!tft) return w;
  int out = (w * tft->width()) / 800;
  return out < 8 ? 8 : out;
}

static int sh(SparkyTft* tft, int h) {
  if (!tft) return h;
  int out = (h * tft->height()) / 480;
  return out < 8 ? 8 : out;
}

static bool useCompactButtonLayout(SparkyTft* tft, const EezMockupScreen* screen) {
  if (!tft || !screen || screen->buttonCount == 0) return false;
  int maxBottom = 0;
  for (size_t i = 0; i < screen->buttonCount; i++) {
    int by = sy(tft, screen->buttons[i].y);
    int bh = sh(tft, screen->buttons[i].h);
    int bottom = by + bh;
    if (bottom > maxBottom) maxBottom = bottom;
  }
  return maxBottom > (tft->height() - 14);
}

static int compactButtonsTop(SparkyTft* tft, const EezMockupScreen* screen) {
  int maxLabelBottom = 0;
  if (!tft || !screen) return 60;
  for (size_t i = 0; i < screen->labelCount; i++) {
    if (screen->labels[i].x >= 350) continue;
    /* EEZ mockup subtitle row(s) — omit in production UI */
    if (screen->labels[i].y >= 38 && screen->labels[i].y <= 50) continue;
    int ly = sy(tft, screen->labels[i].y);
    const bool title = (screen->labels[i].y <= 14 && screen->labels[i].x < 100);
    if (title) {
      if (ly < 28) ly = 28;
      ly += 18; /* ~glyph height at label text size 2 */
    }
    if (ly > maxLabelBottom) maxLabelBottom = ly;
  }
  int top = maxLabelBottom + 14;
  if (top < 56) top = 56;
  if (top > tft->height() - 120) top = 56;
  return top;
}

/**
 * Match Screens.cpp full-width / centered layout (EEZ JSON uses narrow left column — wrong on device).
 */
static void getButtonRect(SparkyTft* tft, const EezMockupScreen* screen, size_t index, int* outX, int* outY, int* outW, int* outH) {
  if (!tft || !screen || index >= screen->buttonCount || !outX || !outY || !outW || !outH) return;
  const ScreenId sid = screen->id;
  const int w = tft->width();
  const int h = tft->height();

  if (sid == SCREEN_MAIN_MENU && index < 3) {
    int y0 = 0, btnH = 0, gap = 0;
    mainMenuButtonLayout(tft, &y0, &btnH, &gap);
    if (OtaUpdate_isInstallOfferPending()) {
      const bool r = readableUi(tft);
      y0 += (r ? 46 : 40) + (r ? 10 : 8);
    }
    *outX = 20;
    *outY = y0 + (int)index * (btnH + gap);
    *outW = w - 40;
    *outH = btnH;
    return;
  }

  if (sid == SCREEN_MODE_SELECT) {
    if (index == 0) {
      *outX = 20;
      *outY = 80;
      *outW = w - 40;
      *outH = 56;
      return;
    }
    if (index == 1) {
      *outX = 20;
      *outY = 150;
      *outW = w - 40;
      *outH = 56;
      return;
    }
    if (index == 2) {
      *outX = w / 2 - 60;
      *outY = 220;
      *outW = 120;
      *outH = 44;
      return;
    }
  }

  if (sid == SCREEN_TEST_SELECT) {
    /* Paginated list is drawn by Screens_drawTestSelectPagedContent; only Back stays in mockup data. */
    if (index == 0) {
      int bw = 0, bh = 0;
      backButtonSize(tft, &bw, &bh);
      *outX = w - bw - 12;
      *outY = layoutLandWide(tft) ? 10 : 8;
      *outW = bw;
      *outH = bh;
      return;
    }
    *outX = 0;
    *outY = -100;
    *outW = 0;
    *outH = 0;
    return;
  }

  if (sid == SCREEN_SETTINGS) {
    const char* lbl = screen->buttons[index].text;
    const bool field = AppState_isFieldMode();
    int y0 = 0, btnH = 0, gap = 0, nRows = 0, backY = 0;
    settingsLayoutDims(w, h, field, &y0, &btnH, &gap, &nRows, &backY);
    if (field && lbl &&
        (strcmp(lbl, "Change PIN") == 0 || strcmp(lbl, "Training sync (PIN)") == 0)) {
      *outX = 0;
      *outY = -100;
      *outW = 0;
      *outH = 0;
      return;
    }
    int row = field ? settingsRowFromLabelField(lbl) : settingsRowFromLabelTraining(lbl);
    if (row >= 0) {
      *outX = 20;
      *outY = y0 + row * (btnH + gap);
      *outW = w - 40;
      *outH = btnH;
      return;
    }
    if (lbl && strcmp(lbl, "Back") == 0) {
      *outX = 20;
      *outY = backY;
      *outW = w - 40;
      *outH = 40;
      return;
    }
  }

  if (sid == SCREEN_ROTATION) {
    if (index == 0) {
      *outX = 20;
      *outY = 60;
      *outW = w - 40;
      *outH = 48;
      return;
    }
    if (index == 1) {
      *outX = 20;
      *outY = 118;
      *outW = w - 40;
      *outH = 48;
      return;
    }
    if (index == 2) {
      *outX = 20;
      *outY = h - 52;
      *outW = w - 40;
      *outH = 44;
      return;
    }
  }

  if (sid == SCREEN_REPORT_LIST) {
    *outX = 20;
    *outY = h - 52;
    *outW = w - 40;
    *outH = 40;
    return;
  }

  if (sid == SCREEN_REPORT_SAVED) {
    *outX = w / 2 - 50;
    *outY = h - 56;
    *outW = 100;
    *outH = 44;
    return;
  }

  if (sid == SCREEN_ABOUT) {
    *outX = 20;
    *outY = h - 44;
    *outW = w - 40;
    *outH = 36;
    return;
  }

  if (sid == SCREEN_WIFI_LIST) {
    if (index == 0) {
      *outX = 20;
      *outY = 62;
      *outW = 100;
      *outH = 28;
      return;
    }
    if (index == 1) {
      *outX = 20;
      *outY = 98;
      *outW = w - 40;
      *outH = 28;
      return;
    }
    if (index == 2) {
      *outX = 20;
      *outY = h - 42;
      *outW = 80;
      *outH = 34;
      return;
    }
  }

  if (!useCompactButtonLayout(tft, screen)) {
    *outX = 20;
    *outY = sy(tft, screen->buttons[index].y);
    *outW = w - 40;
    int hh = sh(tft, screen->buttons[index].h);
    if (hh < 36) hh = 36;
    *outH = hh;
    return;
  }

  int n = (int)screen->buttonCount;
  int left = 14;
  int width = tft->width() - 2 * left;
  int top = compactButtonsTop(tft, screen);
  int gap = 4;
  int usableH = tft->height() - top - 10;
  int rowH = (usableH - (n - 1) * gap) / (n > 0 ? n : 1);
  if (rowH < 14) rowH = 14;
  /* Slightly taller caps on 800-class panels for text size 2–3 in stacked rows */
  if (rowH > (tft->width() >= 700 ? 44 : 40)) rowH = (tft->width() >= 700 ? 44 : 40);
  *outX = left;
  *outY = top + (int)index * (rowH + gap);
  *outW = width;
  *outH = rowH;
}

static uint8_t mockupLabelTextSize(SparkyTft* tft) {
  if (!tft) return 1;
  if (readableUi(tft)) return 2;
  if (tft->width() >= 400) return 2;
  return 1;
}

static uint8_t mockupButtonTextSize(SparkyTft* tft) {
  if (!tft) return 1;
  if (tft->width() >= 700) return 3;
  if (readableUi(tft)) return 2;
  if (tft->width() >= 400) return 2;
  return 1;
}

static uint8_t eeButtonTextSize(SparkyTft* tft, ScreenId sid, const char* text) {
  if (!tft) return 1;
  const int W = tft->width();
  const bool land = W >= 700;
  const bool r = readableUi(tft);
  if (sid == SCREEN_MAIN_MENU) return r ? 2 : 1;
  if (sid == SCREEN_MODE_SELECT) return r ? 2 : 1;
  if (sid == SCREEN_TEST_SELECT) {
    if (text && strcmp(text, "Back") == 0) return r ? 2 : 1;
    if (land) return 3;
    if (r) return 2;
    return 2;
  }
  if (sid == SCREEN_SETTINGS || sid == SCREEN_ROTATION || sid == SCREEN_WIFI_LIST || sid == SCREEN_REPORT_LIST ||
      sid == SCREEN_REPORT_VIEW || sid == SCREEN_REPORT_SAVED || sid == SCREEN_ABOUT)
    return r ? 2 : 1;
  return mockupButtonTextSize(tft);
}

static void drawButton(SparkyTft* tft, int x, int y, int w, int h, const char* text, bool accent, uint8_t tsIn) {
  uint16_t bg = accent ? kGreen : kBtn;
  uint16_t fg = kWhite;
  tft->fillRoundRect(x, y, w, h, 8, bg);
  tft->drawRoundRect(x, y, w, h, 8, kWhite);
  const char* t = text ? text : "";
  size_t len = strlen(t);
  uint8_t ts = tsIn < 1 ? 1 : tsIn;
  uint8_t minTs = (tft && readableUi(tft)) ? 2 : 1;
  if (minTs == 2 && (int)len * 6 * 2 > w - 12) minTs = 1;
  while (ts > minTs && (int)len * 6 * (int)ts > w - 12) ts--;
  tft->setTextSize(ts);
  tft->setTextWrap(false);
  tft->setTextColor(fg, bg);
  int tw = (int)len * 6 * (int)ts;
  int tx = x + (w - tw) / 2;
  if (tx < x + 4) tx = x + 4;
  /* Adafruit classic font: ~7px glyph height at scale 1; center in button */
  const int fh = 7 * (int)ts;
  int ty = y + (h - fh) / 2;
  if (ty < y + 2) ty = y + 2;
  if (ty + fh > y + h - 1) ty = y + h - fh - 1;
  tft->setCursor(tx, ty);
  tft->print(t);
}

static bool screenButtonCenterLegacy(ScreenId screen, const char* label, int w, int h, int* outX, int* outY) {
  if (!label || !outX || !outY) return false;
  const bool panelWide = (w >= 700);

  if (screen == SCREEN_MAIN_MENU) {
    int y0 = 0, btnH = 0, gap = 0;
    mainMenuButtonLayoutDims(w, h, &y0, &btnH, &gap);
    const int y1 = y0 + btnH + gap;
    const int y2 = y1 + btnH + gap;
    if (strcmp(label, "Start verification") == 0) {
      *outX = w / 2;
      *outY = y0 + btnH / 2;
      return true;
    }
    if (strcmp(label, "View reports") == 0) {
      *outX = w / 2;
      *outY = y1 + btnH / 2;
      return true;
    }
    if (strcmp(label, "Settings") == 0) {
      *outX = w / 2;
      *outY = y2 + btnH / 2;
      return true;
    }
    return false;
  }

  if (screen == SCREEN_MODE_SELECT) {
    if (strcmp(label, "Training (apprentice/supervised)") == 0) {
      *outX = w / 2;
      *outY = 80 + 28;
      return true;
    }
    if (strcmp(label, "Field (qualified electrician)") == 0) {
      *outX = w / 2;
      *outY = 150 + 28;
      return true;
    }
    if (strcmp(label, "Continue") == 0) {
      *outX = w / 2;
      *outY = 220 + 22;
      return true;
    }
    return false;
  }

  if (screen == SCREEN_TEST_SELECT) {
    if (strcmp(label, "Back") == 0) {
      int bw = 0, bh = 0;
      backButtonSizeDims(w, &bw, &bh);
      const int backX = w - bw - 12;
      const int backY = panelWide ? 10 : 8;
      *outX = backX + bw / 2;
      *outY = backY + bh / 2;
      return true;
    }
    return false;
  }

  if (screen == SCREEN_REPORT_SAVED) {
    if (strcmp(label, "OK") == 0) {
      *outX = w / 2;
      *outY = (h - 56) + 22;
      return true;
    }
    return false;
  }

  if (screen == SCREEN_REPORT_LIST) {
    if (strcmp(label, "Back") == 0) {
      *outX = 70;
      *outY = (h - 52) + 20;
      return true;
    }
    return false;
  }

  if (screen == SCREEN_SETTINGS) {
    int y0 = 0, btnH = 0, gap = 0, nRows = 0, backY = 0;
    bool field = AppState_isFieldMode();
    settingsLayoutDims(w, h, field, &y0, &btnH, &gap, &nRows, &backY);
    int row = -1;
    if (strcmp(label, "Screen rotation") == 0) row = 0;
    else if (strcmp(label, "WiFi connection") == 0) row = 1;
    else if (strcmp(label, "Buzzer (sound)") == 0) row = 2;
    else if (strcmp(label, "Date & time") == 0) row = 3;
    else if (strcmp(label, "About") == 0) row = 4;
    else if (strcmp(label, "Firmware updates") == 0) row = 5;
    else if (!field && strcmp(label, "Training sync (PIN)") == 0) row = 6;
    else if (strcmp(label, "Email settings") == 0) row = field ? 6 : 7;
    else if (strcmp(label, "Mode change (boot hold)") == 0) row = field ? 7 : 8;
    else if (strcmp(label, "Restart device") == 0) row = field ? 8 : 9;
    else if (!field && strcmp(label, "Change PIN") == 0) row = 10;

    if (row >= 0) {
      *outX = w / 2;
      *outY = y0 + row * (btnH + gap) + btnH / 2;
      return true;
    }
    if (strcmp(label, "Back") == 0) {
      *outX = w / 2;
      *outY = backY + 20;
      return true;
    }
    return false;
  }

  if (screen == SCREEN_ROTATION) {
    if (strcmp(label, "Portrait") == 0) {
      *outX = w / 2;
      *outY = 60 + 24;
      return true;
    }
    if (strcmp(label, "Landscape") == 0) {
      *outX = w / 2;
      *outY = 118 + 24;
      return true;
    }
    if (strcmp(label, "Back") == 0) {
      *outX = w / 2;
      *outY = h - 52 + 22;
      return true;
    }
    return false;
  }

  if (screen == SCREEN_WIFI_LIST) {
    if (strcmp(label, "Scan") == 0) {
      *outX = 70;
      *outY = 62 + 14;
      return true;
    }
    if (strcmp(label, "SSID row(s)") == 0) {
      *outX = 40;
      *outY = 98 + 15;
      return true;
    }
    if (strcmp(label, "Back") == 0) {
      *outX = 60;
      *outY = (h - 42) + 17;
      return true;
    }
    return false;
  }

  if (screen == SCREEN_ABOUT) {
    if (strcmp(label, "Back") == 0) {
      *outX = 60;
      *outY = (h - 44) + 18;
      return true;
    }
    return false;
  }

  if (screen == SCREEN_UPDATES) {
    int btnY = 150;
    int btnH = 30;
    int gap = 6;
    int half = (w - 50) / 2;
    if (strcmp(label, "Check now") == 0) {
      *outX = w / 2;
      *outY = btnY + btnH / 2;
      return true;
    }
    btnY += btnH + gap;
    if (strcmp(label, "Install now") == 0) {
      *outX = w / 2;
      *outY = btnY + btnH / 2;
      return true;
    }
    btnY += btnH + gap;
    if (strcmp(label, "Toggle auto-check") == 0) {
      *outX = 20 + half / 2;
      *outY = btnY + btnH / 2;
      return true;
    }
    if (strcmp(label, "Toggle auto-install") == 0) {
      *outX = 30 + half + half / 2;
      *outY = btnY + btnH / 2;
      return true;
    }
    btnY += btnH + gap;
    if (strcmp(label, "Back") == 0) {
      *outX = 60;
      *outY = btnY + 13;
      return true;
    }
    return false;
  }

  if (screen == SCREEN_TRAINING_SYNC) {
    int y = 152;
    int btnH = 22;
    int gap = 3;
    int half = (w - 50) / 2;
    if (strcmp(label, "Email On/Off") == 0) {
      *outX = 20 + half / 2;
      *outY = y + btnH / 2;
      return true;
    }
    if (strcmp(label, "Cloud On/Off") == 0) {
      *outX = 30 + half + half / 2;
      *outY = y + btnH / 2;
      return true;
    }
    y += btnH + gap;
    if (strcmp(label, "Cycle target (Auto/Google/SharePoint)") == 0) {
      *outX = w / 2;
      *outY = y + btnH / 2;
      return true;
    }
    y += btnH + gap;
    if (strcmp(label, "Edit endpoint") == 0) {
      *outX = w / 2;
      *outY = y + btnH / 2;
      return true;
    }
    y += btnH + gap;
    if (strcmp(label, "Edit token") == 0) {
      *outX = w / 2;
      *outY = y + btnH / 2;
      return true;
    }
    y += btnH + gap;
    if (strcmp(label, "Edit cubicle") == 0) {
      *outX = w / 2;
      *outY = y + btnH / 2;
      return true;
    }
    y += btnH + gap;
    if (strcmp(label, "Edit device label") == 0) {
      *outX = w / 2;
      *outY = y + btnH / 2;
      return true;
    }
    y += btnH + gap;
    if (strcmp(label, "Send test ping") == 0) {
      *outX = w / 2;
      *outY = y + btnH / 2;
      return true;
    }
    y += btnH + gap;
    if (strcmp(label, "Back") == 0) {
      *outX = 60;
      *outY = y + btnH / 2;
      return true;
    }
    return false;
  }

  if (screen == SCREEN_EMAIL_SETTINGS) {
    int editY = 38 + 5 * 28 + 6;
    if (strcmp(label, "Edit SMTP server") == 0) {
      *outX = w / 2;
      *outY = editY + 13;
      return true;
    }
    if (strcmp(label, "Edit port") == 0) {
      *outX = w / 2;
      *outY = editY + 28 + 13;
      return true;
    }
    if (strcmp(label, "Edit sender email") == 0) {
      *outX = w / 2;
      *outY = editY + 56 + 13;
      return true;
    }
    if (strcmp(label, "Edit SMTP password") == 0) {
      *outX = w / 2;
      *outY = editY + 84 + 13;
      return true;
    }
    if (strcmp(label, "Edit recipient/teacher email") == 0) {
      *outX = w / 2;
      *outY = editY + 112 + 13;
      return true;
    }
    if (strcmp(label, "Back") == 0) {
      *outX = 60;
      *outY = (h - 38) + 15;
      return true;
    }
    return false;
  }

  return false;
}

/**
 * If EEZ label text drifts from Screens.cpp strings, map by button index to the same
 * synthetic point Screens_handleTouch expects (required for test rows, mode, main menu).
 */
static bool screenButtonCenterByIndex(ScreenId screen, size_t index, int w, int h, int* outX, int* outY) {
  (void)h;
  if (!outX || !outY) return false;
  const bool panelWide = (w >= 700);

  switch (screen) {
    case SCREEN_MODE_SELECT:
      if (index == 0) {
        *outX = w / 2;
        *outY = 80 + 28;
        return true;
      }
      if (index == 1) {
        *outX = w / 2;
        *outY = 150 + 28;
        return true;
      }
      if (index == 2) {
        *outX = w / 2;
        *outY = 220 + 22;
        return true;
      }
      return false;
    case SCREEN_MAIN_MENU: {
      if (index > 2) return false;
      int y0 = 0, btnH = 0, gap = 0;
      mainMenuButtonLayoutDims(w, h, &y0, &btnH, &gap);
      if (OtaUpdate_isInstallOfferPending()) {
        const bool r = (w >= 700) || (h >= 700) || (w >= 480 && h >= 600);
        y0 += (r ? 46 : 40) + (r ? 10 : 8);
      }
      const int y1 = y0 + btnH + gap;
      const int y2 = y1 + btnH + gap;
      const int yc[3] = {y0 + btnH / 2, y1 + btnH / 2, y2 + btnH / 2};
      *outX = w / 2;
      *outY = yc[index];
      return true;
    }
    case SCREEN_TEST_SELECT: {
      if (index == 0) {
        int bw = 0, bh = 0;
        backButtonSizeDims(w, &bw, &bh);
        const int backX = w - bw - 12;
        const int backY = panelWide ? 10 : 8;
        *outX = backX + bw / 2;
        *outY = backY + bh / 2;
        return true;
      }
      return false;
    }
    default:
      return false;
  }
}

}  // namespace

void EezMockupUi_draw(SparkyTft* tft, ScreenId id) {
  if (!tft) return;

  if (isLegacyOnlyScreen(id)) {
    Screens_draw(tft, id);
    return;
  }

  const EezMockupScreen* screen = EezMockupData_findScreen(id);
  if (!screen) {
    Screens_draw(tft, id);
    return;
  }

  tft->fillScreen(kBg);
  uint8_t lblTs = mockupLabelTextSize(tft);
  tft->setTextSize(lblTs);
  tft->setTextColor(kWhite, kBg);
  tft->setTextWrap(false);

  /* Omit design-note column and EEZ subtitle row (y≈42). */
  for (size_t i = 0; i < screen->labelCount; i++) {
    if (screen->labels[i].x >= 350) continue;
    /* EEZ mockup subtitle row(s) — omit in production UI */
    if (screen->labels[i].y >= 38 && screen->labels[i].y <= 50) continue;
    const char* txt = screen->labels[i].text ? screen->labels[i].text : "";
    if (!txt[0]) continue;
    int yp = sy(tft, screen->labels[i].y);
    if (yp < 0 || yp >= tft->height()) continue;
    const bool title = (screen->labels[i].y <= 14 && screen->labels[i].x < 100);
    if (title) {
      int tw = (int)strlen(txt) * 6 * (int)lblTs;
      int xp = (tft->width() - tw) / 2;
      if (xp < 8) xp = 8;
      if (yp < 28) yp = 28;
      tft->setCursor(xp, yp);
    } else {
      tft->setCursor(sx(tft, screen->labels[i].x), yp);
    }
    tft->print(txt);
  }

  if (screen->id == SCREEN_MAIN_MENU) {
    tft->setTextSize(1);
    tft->setTextColor(kAccent, kBg);
    const char* mode = AppState_isFieldMode() ? "Field mode" : "Training mode";
    int twm = (int)strlen(mode) * 6;
    const int modeY = layoutLandWide(tft) ? 54 : (tft->height() >= 700 ? 62 : 50);
    tft->setCursor((tft->width() - twm) / 2, modeY);
    tft->print(mode);
    if (OtaUpdate_isInstallOfferPending()) {
      const bool r = readableUi(tft);
      const int bannerY = modeY + 14;
      const int bannerH = r ? 46 : 40;
      const int W = tft->width();
      tft->fillRoundRect(20, bannerY, W - 40, bannerH, 8, kAccent);
      tft->drawRoundRect(20, bannerY, W - 40, bannerH, 8, kWhite);
      tft->setTextColor(TFT_BLACK, kAccent);
      char pv[OTA_VERSION_LEN];
      OtaUpdate_getPendingVersion(pv, sizeof(pv));
      char line[56];
      if (pv[0])
        snprintf(line, sizeof(line), "Firmware v%s — tap for Yes/No", pv);
      else
        strncpy(line, "Firmware update — tap here", sizeof(line) - 1);
      line[sizeof(line) - 1] = '\0';
      int twl = (int)strlen(line) * 6;
      if (twl > W - 48) {
        strncpy(line, "Update ready — tap here", sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
        twl = (int)strlen(line) * 6;
      }
      tft->setCursor((W - twl) / 2, bannerY + (r ? 16 : 13));
      tft->print(line);
    }
  }

  if (screen->id == SCREEN_SETTINGS) {
    const int W = tft->width();
    const bool panelWide = W >= 700;
    const int backW = panelWide ? 96 : 92;
    const int backH = 36;
    const int backX = W - backW - 12;
    const int backY = panelWide ? 10 : 8;
    tft->fillRoundRect(backX, backY, backW, backH, 6, kBtn);
    tft->drawRoundRect(backX, backY, backW, backH, 6, kWhite);
    tft->setTextSize(2);
    tft->setTextColor(kWhite, kBtn);
    const char* b = "Back";
    int twb = (int)strlen(b) * 6 * 2;
    tft->setCursor(backX + (backW - twb) / 2, backY + (backH - 14) / 2);
    tft->print(b);
    Screens_drawSettingsPagedContent(tft);
  }

  if (screen->id == SCREEN_TEST_SELECT) {
    const int ts = layoutLandWide(tft) ? 2 : (readableUi(tft) ? 2 : 1);
    tft->setTextSize(ts);
    tft->setTextColor(kAccent, kBg);
    char scope[96];
    Standards_getVerificationScopeLine(scope, sizeof(scope));
    const char* key = " Sec 8 & ";
    char* hit = strstr(scope, key);
    const int yScope = layoutLandWide(tft) ? 56 : (tft->height() >= 700 ? 58 : 50);
    if (hit) {
      *hit = '\0';
      const char* b = hit + strlen(key);
      int tw1 = (int)strlen(scope) * 6 * ts;
      int tw2 = (int)strlen(b) * 6 * ts;
      tft->setCursor((tft->width() - tw1) / 2, yScope);
      tft->print(scope);
      tft->setCursor((tft->width() - tw2) / 2, yScope + 7 * ts + 2);
      tft->print(b);
    } else {
      tft->setTextWrap(true);
      tft->setCursor(20, yScope);
      tft->print(scope);
      tft->setTextWrap(false);
    }
    Screens_drawTestSelectPagedContent(tft);
  }

  for (size_t i = 0; i < screen->buttonCount; i++) {
    if (screen->id == SCREEN_SETTINGS) continue;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    getButtonRect(tft, screen, i, &x, &y, &w, &h);
    if (h <= 0 || y < -50) continue;
    bool accent = isAccentButton(screen->buttons[i].text);
    if (y >= tft->height()) continue;
    uint8_t btnTs = eeButtonTextSize(tft, screen->id, screen->buttons[i].text);
    drawButton(tft, x, y, w, h, screen->buttons[i].text, accent, btnTs);
  }

  Screens_drawStatusBar(tft);
  sparkyDisplayFlush(tft);
}

ScreenId EezMockupUi_handleTouch(SparkyTft* tft, ScreenId current, uint16_t x, uint16_t y) {
  s_handledButton = false;
  if (!tft) return current;

  if (isLegacyOnlyScreen(current)) {
    ScreenId next = Screens_handleTouch(tft, current, x, y);
    s_handledButton = Screens_didHandleButton();
    return next;
  }

  const EezMockupScreen* screen = EezMockupData_findScreen(current);
  if (!screen) {
    ScreenId next = Screens_handleTouch(tft, current, x, y);
    s_handledButton = Screens_didHandleButton();
    return next;
  }

  if (current == SCREEN_TEST_SELECT) {
    ScreenId next = Screens_handleTouch(tft, current, x, y);
    s_handledButton = Screens_didHandleButton();
    return next;
  }

  if (current == SCREEN_SETTINGS) {
    ScreenId next = Screens_handleTouch(tft, current, x, y);
    s_handledButton = Screens_didHandleButton();
    return next;
  }

  int ix = (int)x;
  int iy = (int)y;

  if (current == SCREEN_MAIN_MENU && OtaUpdate_isInstallOfferPending()) {
    const bool r = readableUi(tft);
    const int bannerH = r ? 46 : 40;
    const int modeY = layoutLandWide(tft) ? 54 : (tft->height() >= 700 ? 62 : 50);
    const int bannerY = modeY + 14;
    const int Wm = tft->width();
    if (inRectPad(ix, iy, 20, bannerY, Wm - 40, bannerH, 3)) {
      s_handledButton = true;
      return Screens_handleTouch(tft, current, (uint16_t)(Wm / 2), (uint16_t)(bannerY + bannerH / 2));
    }
  }

  for (size_t i = 0; i < screen->buttonCount; i++) {
    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    getButtonRect(tft, screen, i, &bx, &by, &bw, &bh);
    if (!inRectPad(ix, iy, bx, by, bw, bh, 3)) continue;

    s_handledButton = true;

    if (current == SCREEN_ROTATION) {
      const char* lbl = screen->buttons[i].text;
      if (lbl && strcmp(lbl, "Portrait") == 0) {
        AppState_setRotation(0);
        Screens_showSavedPrompt(tft, "Display: Portrait");
        return SCREEN_SETTINGS;
      }
      if (lbl && strcmp(lbl, "Landscape") == 0) {
        AppState_setRotation(1);
        Screens_showSavedPrompt(tft, "Display: Landscape");
        return SCREEN_SETTINGS;
      }
      if (lbl && strcmp(lbl, "Back") == 0) return SCREEN_SETTINGS;
    }

    int lx = 0;
    int ly = 0;
    bool haveLegacy = screenButtonCenterLegacy(current, screen->buttons[i].text, tft->width(), tft->height(), &lx, &ly);
    if (!haveLegacy) {
      haveLegacy = screenButtonCenterByIndex(current, i, tft->width(), tft->height(), &lx, &ly);
    }
    if (haveLegacy) {
      uint16_t tx = (uint16_t)lx;
      uint16_t ty = (uint16_t)ly;
      ScreenId next = Screens_handleTouch(tft, current, tx, ty);
      if (next == current && EezMockupData_findScreen(current)) {
        EezMockupUi_draw(tft, current);
      }
      return next;
    }

    /* When mockup layout matches legacy (non-compact), raw coords work for WiFi list, About, etc. */
    if (!useCompactButtonLayout(tft, screen)) {
      ScreenId n = Screens_handleTouch(tft, current, (uint16_t)ix, (uint16_t)iy);
      if (Screens_didHandleButton() || n != current) {
        if (n == current && EezMockupData_findScreen(current)) EezMockupUi_draw(tft, current);
        s_handledButton = Screens_didHandleButton();
        return n;
      }
    }

    /* Last resort: static screen link only (no Screens state updates). */
    return screen->buttons[i].target;
  }

  return current;
}

ScreenId EezMockupUi_handleTouchDrag(SparkyTft* tft, ScreenId current, uint16_t x, uint16_t y) {
  return Screens_handleTouchDrag(tft, current, x, y);
}

ScreenId EezMockupUi_handleTouchEnd(SparkyTft* tft, ScreenId current, uint16_t x, uint16_t y) {
  return Screens_handleTouchEnd(tft, current, x, y);
}

bool EezMockupUi_didHandleButton(void) {
  return s_handledButton;
}
