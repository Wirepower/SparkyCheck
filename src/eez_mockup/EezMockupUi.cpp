#include "EezMockupUi.h"

#include <ctype.h>
#include <string.h>

#include "AppState.h"
#include "EezMockupData.h"
#include "Screens.h"

namespace {

static const uint16_t kBg = 0x18E3;
static const uint16_t kBtn = 0x2D6A;
static const uint16_t kAccent = 0xFD20;
static const uint16_t kWhite = 0xFFFF;
static const uint16_t kGreen = 0x07E0;
static const uint16_t kBlack = 0x0000;

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
    case SCREEN_WIFI_PASSWORD:
    case SCREEN_TRAINING_SYNC_EDIT:
    case SCREEN_EMAIL_FIELD_EDIT:
    case SCREEN_CHANGE_PIN:
    case SCREEN_PIN_ENTER:
      return true;
    default:
      return false;
  }
}

static bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh);
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
    int ly = sy(tft, screen->labels[i].y);
    if (ly > maxLabelBottom) maxLabelBottom = ly;
  }
  int top = maxLabelBottom + 14;
  if (top < 56) top = 56;
  if (top > tft->height() - 120) top = 56;
  return top;
}

static void getButtonRect(SparkyTft* tft, const EezMockupScreen* screen, size_t index, int* outX, int* outY, int* outW, int* outH) {
  if (!tft || !screen || index >= screen->buttonCount || !outX || !outY || !outW || !outH) return;
  if (!useCompactButtonLayout(tft, screen)) {
    *outX = sx(tft, screen->buttons[index].x);
    *outY = sy(tft, screen->buttons[index].y);
    *outW = sw(tft, screen->buttons[index].w);
    *outH = sh(tft, screen->buttons[index].h);
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
  if (rowH > 40) rowH = 40;
  *outX = left;
  *outY = top + (int)index * (rowH + gap);
  *outW = width;
  *outH = rowH;
}

static void drawButton(SparkyTft* tft, int x, int y, int w, int h, const char* text, bool accent) {
  uint16_t bg = accent ? kGreen : kBtn;
  uint16_t fg = accent ? kBlack : kWhite;
  tft->fillRoundRect(x, y, w, h, 8, bg);
  tft->drawRoundRect(x, y, w, h, 8, kWhite);
  tft->setTextSize(1);
  tft->setTextColor(fg, bg);
  int tw = (int)strlen(text ? text : "") * 6;
  int tx = x + (w - tw) / 2;
  if (tx < x + 4) tx = x + 4;
  int ty = y + (h / 2) - 4;
  tft->setCursor(tx, ty);
  tft->print(text ? text : "");
}

static bool screenButtonCenterLegacy(ScreenId screen, const char* label, int w, int h, int* outX, int* outY) {
  if (!label || !outX || !outY) return false;
  const bool panelWide = (w >= 700);

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

  if (screen == SCREEN_MAIN_MENU) {
    int btnH = panelWide ? 64 : 44;
    int y0 = panelWide ? 88 : 76;
    int y1 = y0 + btnH + (panelWide ? 14 : 12);
    int y2 = y1 + btnH + (panelWide ? 14 : 12);
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

  if (screen == SCREEN_TEST_SELECT) {
    int row = -1;
    if (strcmp(label, "Earth continuity (conductors)") == 0) row = 0;
    else if (strcmp(label, "Insulation resistance") == 0) row = 1;
    else if (strcmp(label, "Polarity") == 0) row = 2;
    else if (strcmp(label, "Earth continuity (CPC)") == 0) row = 3;
    else if (strcmp(label, "Correct circuit connections") == 0) row = 4;
    else if (strcmp(label, "Earth fault loop impedance") == 0) row = 5;
    else if (strcmp(label, "RCD operation") == 0) row = 6;
    else if (strcmp(label, "SWP D/R (motor)") == 0) row = 7;
    else if (strcmp(label, "SWP D/R (appliance)") == 0) row = 8;
    else if (strcmp(label, "SWP D/R (heater/sheathed)") == 0) row = 9;

    if (row >= 0) {
      int rowH = panelWide ? 38 : 18;
      int y0 = panelWide ? 84 : 48;
      *outX = w / 2;
      *outY = y0 + row * rowH + (rowH / 2);
      return true;
    }

    if (strcmp(label, "Back") == 0) {
      int backW = panelWide ? 90 : 48;
      int backH = panelWide ? 34 : 24;
      int backX = w - (panelWide ? 102 : 62);
      int backY = panelWide ? 10 : 8;
      *outX = backX + backW / 2;
      *outY = backY + backH / 2;
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
    int btnH = 30;
    int gap = 2;
    int y = 42;
    bool field = AppState_isFieldMode();
    int nRows = field ? 7 : 8;
    int row = -1;
    if (strcmp(label, "Screen rotation") == 0) row = 0;
    else if (strcmp(label, "WiFi connection") == 0) row = 1;
    else if (strcmp(label, "Buzzer (sound)") == 0) row = 2;
    else if (strcmp(label, "About") == 0) row = 3;
    else if (strcmp(label, "Firmware updates") == 0) row = 4;
    else if (strcmp(label, "Email settings") == 0) row = 5;
    else if (strcmp(label, "Mode change (boot hold)") == 0) row = 6;
    else if (strcmp(label, "Change PIN") == 0) row = 7;

    if (row >= 0) {
      if (field && row >= 7) return false;
      *outX = w / 2;
      *outY = y + row * (btnH + gap) + btnH / 2;
      return true;
    }
    if (strcmp(label, "Back") == 0) {
      int backY = 42 + nRows * (btnH + gap);
      *outX = 60;
      *outY = backY + 14;
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
      *outX = 60;
      *outY = 178 + 18;
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
    if (strcmp(label, "Training sync setup (PIN)") == 0) {
      *outX = w / 2;
      *outY = btnY + btnH / 2;
      return true;
    }
    if (!AppState_isFieldMode()) btnY += btnH + gap;
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
  tft->setTextSize(1);
  tft->setTextColor(kWhite, kBg);

  for (size_t i = 0; i < screen->labelCount; i++) {
    int x = sx(tft, screen->labels[i].x);
    int y = sy(tft, screen->labels[i].y);
    if (y < 0 || y >= tft->height()) continue;
    tft->setCursor(x, y);
    tft->print(screen->labels[i].text ? screen->labels[i].text : "");
  }

  for (size_t i = 0; i < screen->buttonCount; i++) {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    getButtonRect(tft, screen, i, &x, &y, &w, &h);
    bool accent = isAccentButton(screen->buttons[i].text);
    if (y >= tft->height()) continue;
    drawButton(tft, x, y, w, h, screen->buttons[i].text, accent);
  }

  // Simple footer to indicate that this path is EEZ-derived.
  tft->setTextColor(kAccent, kBg);
  tft->setCursor(8, tft->height() - 10);
  tft->print("EEZ mockup runtime");
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

  int ix = (int)x;
  int iy = (int)y;
  for (size_t i = 0; i < screen->buttonCount; i++) {
    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    getButtonRect(tft, screen, i, &bx, &by, &bw, &bh);
    if (!inRect(ix, iy, bx, by, bw, bh)) continue;

    s_handledButton = true;

    int lx = 0;
    int ly = 0;
    if (screenButtonCenterLegacy(current, screen->buttons[i].text, tft->width(), tft->height(), &lx, &ly)) {
      ScreenId next = Screens_handleTouch(tft, current, (uint16_t)lx, (uint16_t)ly);
      return next;
    }

    // Fallback to static EEZ mockup navigation mapping.
    return screen->buttons[i].target;
  }

  return current;
}

bool EezMockupUi_didHandleButton(void) {
  return s_handledButton;
}
