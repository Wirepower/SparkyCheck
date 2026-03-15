/**
 * SparkyCheck – First boot screen
 * Creator credit + industry-themed graphic (no external image required).
 */

#include "BootScreen.h"
#include "Standards.h"
#include <string.h>

namespace BootScreen {

static const char* kCreatorCredit = "Created by Frank Offer 2026";

// Colours (TFT_eSPI 16-bit RGB565)
static const uint16_t kBgDark    = 0x18E3;  // Dark blue-grey
static const uint16_t kBgMid     = 0x2D6A;  // Mid blue
static const uint16_t kAccent   = 0xFD20;  // Amber/gold
static const uint16_t kAccentDim = 0xBC40;  // Dimmer gold
static const uint16_t kWhite    = 0xFFFF;
static const uint16_t kGreen    = 0x07E0;

void drawIndustryGraphic(TFT_eSPI& tft, int centerX, int centerY, int size) {
  const int r = size / 2;
  const int cx = centerX;
  const int cy = centerY;

  // Outer ring (verification / “OK” circle)
  tft.drawCircle(cx, cy, r, kAccentDim);
  tft.drawCircle(cx, cy, r - 1, kAccent);
  tft.fillCircle(cx, cy, r - 4, kBgDark);

  // Large checkmark (pass/verification)
  int  tickW = size / 8;
  int  leg   = size / 3;
  int  x0    = cx - leg;
  int  y0    = cy + leg / 2;
  int  x1    = cx - leg / 3;
  int  y1    = cy + leg;
  int  x2    = cx + leg;
  int  y2    = cy - leg;
  for (int d = -tickW; d <= tickW; d++) {
    tft.drawLine(x0 + d, y0, x1 + d, y1, kGreen);
    tft.drawLine(x1 + d, y1, x2 + d, y2, kGreen);
  }
  tft.fillTriangle(x0, y0, x1, y1, x2, y2, kGreen);
  tft.drawTriangle(x0, y0, x1, y1, x2, y2, kWhite);

  // Small “cable” lines (stylized wires) below
  int ly = cy + r - 2;
  tft.drawFastHLine(cx - r + 4, ly,     (r - 4) * 2, kAccentDim);
  tft.drawFastHLine(cx - r + 6, ly + 4, (r - 6) * 2, kAccentDim);
}

void showFirst(TFT_eSPI& tft) {
  const int w = tft.width();
  const int h = tft.height();

  // Background: dark gradient effect (two bands)
  tft.fillRect(0, 0, w, h / 2, kBgDark);
  tft.fillRect(0, h / 2, w, h - h / 2, kBgMid);

  // Top: product name
  tft.setTextColor(kWhite, kBgDark);
  tft.setTextSize(3);
  tft.setCursor(w / 2 - 120, 28);
  tft.print("SparkyCheck");

  // Tagline
  tft.setTextSize(1);
  tft.setTextColor(kAccentDim, kBgDark);
  tft.setCursor(w / 2 - 95, 58);
  tft.print("Electrical Verification Device");

  // Industry graphic (centered in upper area)
  drawIndustryGraphic(tft, w / 2, 130, 100);

  // Creator line – bottom right
  tft.setTextColor(kAccentDim, kBgMid);
  tft.setTextSize(1);
  const char* credit = kCreatorCredit;
  int tw = (int)strlen(credit) * 6;  // Text size 1 default width
  int th = 8;                         // Text size 1 default height
  tft.setCursor(w - tw - 12, h - th - 10);
  tft.print(credit);

  // Hint – centre bottom
  tft.setTextColor(kAccentDim, kBgMid);
  tft.setTextSize(1);
  tft.setCursor(w / 2 - 55, h - 22);
  tft.print("Touch to continue");
}

bool isCreatorCreditTouchRegion(TFT_eSPI& tft, int x, int y) {
  const int w = tft.width();
  const int h = tft.height();
  int tw = (int)strlen(kCreatorCredit) * 6;
  int th = 8;
  int rx = w - tw - 16;
  int ry = h - th - 14;
  int rw = tw + 8;
  int rh = th + 8;
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void showDisclaimer(TFT_eSPI& tft) {
  const int w = tft.width();
  const int h = tft.height();

  tft.fillScreen(kBgDark);

  tft.setTextColor(kWhite, kBgDark);
  tft.setTextSize(2);
  tft.setCursor(20, 8);
  tft.print("Disclaimer");

  tft.setTextColor(kAccentDim, kBgDark);
  tft.setTextSize(1);
  tft.setCursor(20, 36);
  tft.print("SparkyCheck is a guidance tool only. It is NOT a");
  tft.setCursor(20, 50);
  tft.print("verification or testing device. It does not perform");
  tft.setCursor(20, 64);
  tft.print("or substitute for mandatory testing under");
  char d1[80], d2[80];
  Standards_getDisclaimerStandardLines(d1, sizeof(d1), d2, sizeof(d2));
  tft.setCursor(20, 78);
  tft.print(d1);
  tft.setCursor(20, 92);
  tft.print(d2);
  tft.setCursor(20, 110);
  tft.print("Responsibility for correct and compliant");
  tft.setCursor(20, 124);
  tft.print("inspection and verification remains with the user.");
  tft.setCursor(20, 142);
  tft.print("Use approved test equipment; zero leads before use.");
  tft.setCursor(20, 160);
  tft.print("By accepting, you acknowledge these terms.");

  // Accept button (bottom centre) – must tap to continue
  const int btnW = 160, btnH = 44, btnY = h - 52;
  const int btnX = (w - btnW) / 2;
  tft.fillRoundRect(btnX, btnY, btnW, btnH, 6, kGreen);
  tft.drawRoundRect(btnX, btnY, btnW, btnH, 6, kWhite);
  tft.setTextColor(TFT_BLACK, kGreen);
  tft.setTextSize(2);
  tft.setCursor(btnX + 28, btnY + 12);
  tft.print("I Accept");

  uint16_t tx = 0, ty = 0;
  while (true) {
    if (tft.getTouch(&tx, &ty)) {
      if ((int)tx >= btnX && (int)tx < btnX + btnW && (int)ty >= btnY && (int)ty < btnY + btnH)
        break;
    }
    delay(50);
  }
}

}  // namespace BootScreen
