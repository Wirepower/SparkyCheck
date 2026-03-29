/**
 * SparkyCheck – First boot screen
 * Creator credit + boot logo (RGB565 bitmap from include/boot_logo_embedded.h).
 */

#include "BootScreen.h"
#include "SparkyDisplay.h"
#include "Standards.h"
#include "OtaUpdate.h"
#include <Arduino.h>
#include "boot_logo_embedded.h"
#include <math.h>
#include <string.h>

namespace BootScreen {

static const char* kCreatorCredit = "Frank Offer · 2026";

/** Vertical centre of boot logo (matches `drawBootLogoRaster`). */
static int bootGraphicCenterY(int screenH) { return screenH / 2; }

// Colours (TFT_eSPI 16-bit RGB565)
static const uint16_t kBootNearBlack = 0x0841;  // ~#0D1117 behind logo
static const uint16_t kBgDark    = 0x18E3;  // Dark blue-grey (text blend)
static const uint16_t kBgMid     = 0x2D6A;  // Mid blue
static const uint16_t kAccent   = 0xFD20;  // Amber/gold
static const uint16_t kAccentDim = 0xBC40;  // Dimmer gold
static const uint16_t kWhite    = 0xFFFF;
/* Phase / neutral / safety green (reference: pure primaries + black N) */
static const uint16_t kPhaseRed   = 0xF800;  // #FF0000
static const uint16_t kPhaseWhite = 0xFFFF;  // #FFFFFF
static const uint16_t kPhaseBlue  = 0x001F;  // #0000FF (RGB565)
static const uint16_t kNeutralN   = 0x0000;  // #000000
static const uint16_t kNeutralHi  = 0x4208;  // subtle sheen on N
static const uint16_t kRingGlowD  = 0x0200;  // circle / tick glow
static const uint16_t kRingGlowM  = 0x0520;
static const uint16_t kSafetyGreen = 0x07E0;  // lime / safety green ring + tick
static const uint16_t kSafetyHi   = 0x3666;   // highlight stroke

/** Linear blend between two RGB565 colours (t=0 -> a, t=max -> b). */
static uint16_t lerp565(uint16_t a, uint16_t b, int t, int max) {
  if (max <= 0) return a;
  if (t <= 0) return a;
  if (t >= max) return b;
  const uint8_t r1 = (uint8_t)((a >> 11) & 0x1F);
  const uint8_t g1 = (uint8_t)((a >> 5) & 0x3F);
  const uint8_t b1 = (uint8_t)(a & 0x1F);
  const uint8_t r2 = (uint8_t)((b >> 11) & 0x1F);
  const uint8_t g2 = (uint8_t)((b >> 5) & 0x3F);
  const uint8_t b2 = (uint8_t)(b & 0x1F);
  const int inv = max - t;
  const uint8_t r = (uint8_t)((r1 * inv + r2 * t) / max);
  const uint8_t g = (uint8_t)((g1 * inv + g2 * t) / max);
  const uint8_t bl = (uint8_t)((b1 * inv + b2 * t) / max);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

static void fillSkewQuad(SparkyTft& tft, int tlx, int tly, int trx, int try_, int brx, int bry, int blx, int bly,
                         uint16_t c) {
  tft.fillTriangle(tlx, tly, trx, try_, brx, bry, c);
  tft.fillTriangle(tlx, tly, brx, bry, blx, bly, c);
}

/**
 * Reference layout: pure R/W/B phases, black slanted N, safety-green tick (vertex on red, arms A–C),
 * then glowing green circle framing the mark.
 */
void drawIndustryGraphic(SparkyTft& tft, int centerX, int centerY, int size, uint16_t innerBg) {
  (void)innerBg;
  const int barH = (size * 54) / 100;
  const int barW = (size * 22) / 100;
  const int gap = (size * 5) / 100;
  int skew = (barH * 22) / 100;
  if (skew < 2) skew = 2;
  if (barH < 14 || barW < 8) return;

  const int phaseCx = centerX;
  const int yTop = centerY - barH / 2;
  const int span = 3 * barW + 2 * gap + skew;
  const int startX = phaseCx - span / 2;

  const uint16_t phaseCol[3] = {kPhaseRed, kPhaseWhite, kPhaseBlue};
  for (int i = 0; i < 3; i++) {
    const int ox = startX + i * (barW + gap);
    const int tlx = ox;
    const int tly = yTop;
    const int trx = ox + barW;
    const int try_ = yTop;
    const int brx = ox + barW + skew;
    const int bry = yTop + barH;
    const int blx = ox + skew;
    const int bly = yTop + barH;
    fillSkewQuad(tft, tlx, tly, trx, try_, brx, bry, blx, bly, phaseCol[i]);
    if (i == 1) {
      tft.drawLine(tlx, tly + 1, blx, bly - 1, 0x8410);
      tft.drawLine(trx, try_ + 1, brx, bry - 1, 0x8410);
    }
  }

  const int ny = yTop + barH;
  const int nh = (size * 14) / 100;
  if (nh < 7) return;
  const int nLeft = startX + skew;
  const int nWide = 3 * barW + 2 * gap;
  fillSkewQuad(tft, nLeft, ny, nLeft + nWide, ny, nLeft + nWide + skew, ny + nh, nLeft + skew, ny + nh, kNeutralN);
  tft.drawFastHLine(nLeft + 2, ny + nh / 2, nWide - 4, kNeutralHi);

  /* Tick: bottom vertex B on lower-left red; A = left arm; C = top-right across W/B */
  const int xB = startX + (barW * 22) / 100;
  const int yB = yTop + (barH * 86) / 100;
  const int xA = startX - (size * 14) / 100;
  const int yA = yTop + (barH * 58) / 100;
  const int xC = startX + 3 * barW + 2 * gap + (skew * 92) / 100;
  const int yC = yTop + (barH * 10) / 100;

  for (int g = 7; g >= 2; g--) {
    const int o = g * 2;
    tft.fillTriangle(xA - o / 2, yA + o / 5, xB, yB + o / 4, xC + o / 3, yC - o / 4, kRingGlowD);
  }
  for (int g = 3; g >= 1; g--) {
    const int o = g * 2;
    tft.fillTriangle(xA - o / 3, yA + o / 6, xB, yB + o / 5, xC + o / 4, yC - o / 5, kRingGlowM);
  }
  tft.fillTriangle(xA, yA, xB, yB, xC, yC, kSafetyGreen);
  tft.drawLine(xA + 1, yA, xB - 1, yB - 1, kSafetyHi);
  tft.drawLine(xB + 1, yB - 1, xC - 1, yC + 1, kSafetyHi);

  /* Bounding box → center & radius for enclosing circle */
  int minX = startX - 2;
  int maxX = startX + 3 * barW + 2 * gap + skew + 2;
  int minY = yTop - 2;
  int maxY = ny + nh + 2;
  if (xA < minX) minX = xA;
  if (xC > maxX) maxX = xC;
  if (yC < minY) minY = yC;

  const int cx = (minX + maxX) / 2;
  const int cy = (minY + maxY) / 2;
  const int halfW = (maxX - minX + 1) / 2;
  const int halfH = (maxY - minY + 1) / 2;
  int ringR = (int)(sqrtf((float)(halfW * halfW + halfH * halfH))) + (size * 6) / 100;
  if (ringR < size / 3) ringR = size / 3;

  for (int g = 10; g >= 1; g--) {
    tft.drawCircle(cx, cy, ringR + g, kRingGlowD);
  }
  for (int g = 2; g >= 0; g--) {
    tft.drawCircle(cx, cy, ringR + g, kRingGlowM);
  }
  tft.drawCircle(cx, cy, ringR, kSafetyGreen);
  tft.drawCircle(cx, cy, ringR - 1, kSafetyHi);
}

static void drawBootLogoRaster(SparkyTft& tft, int w, int h) {
  const int graphicY = bootGraphicCenterY(h);
  const int x = w / 2 - BOOT_LOGO_W / 2;
  const int y = graphicY - BOOT_LOGO_H / 2;
#if defined(SPARKYCHECK_PANEL_43B)
  tft.drawRGBBitmap(x, y, kBootLogoRgb565, BOOT_LOGO_W, BOOT_LOGO_H);
#else
  tft.pushImage(x, y, BOOT_LOGO_W, BOOT_LOGO_H, (uint16_t*)kBootLogoRgb565);
#endif
}

void showFirst(SparkyTft& tft) {
  const int w = tft.width();
  const int h = tft.height();

  const int bands = 16;
  for (int i = 0; i < bands; i++) {
    uint16_t c = lerp565(kBootNearBlack, kBgMid, i, bands - 1);
    int y0 = (i * h) / bands;
    int y1 = ((i + 1) * h) / bands;
    tft.fillRect(0, y0, w, y1 - y0 + 1, c);
  }

  tft.setTextWrap(false);

  drawBootLogoRaster(tft, w, h);

  /* White on dark bg — readable on the blue gradient (avoids dim gold on mid-blue). */
  tft.setTextColor(kWhite, kBgDark);
  const uint8_t footTs = 2;
  tft.setTextSize(footTs);
  const int cw = 6 * (int)footTs;
  const int footY = h - (7 * (int)footTs + 8);

  {
    const char* ver = OtaUpdate_getCurrentVersion();
    char vbuf[40];
    if (ver && ver[0]) {
      snprintf(vbuf, sizeof(vbuf), "v%s", ver);
    } else {
      strncpy(vbuf, "v?", sizeof(vbuf) - 1);
      vbuf[sizeof(vbuf) - 1] = '\0';
    }
    tft.setCursor(12, footY);
    tft.print(vbuf);
  }

  {
    int tw = (int)strlen(kCreatorCredit) * cw;
    int tx = w - tw - 12;
    tft.setCursor(tx, footY);
    tft.print(kCreatorCredit);
  }

  /* No "Tap to continue" caption on boot logo screen. */
}

bool isBootLogoTouchRegion(SparkyTft& tft, int x, int y) {
  const int w = tft.width();
  const int h = tft.height();
  const int cy = bootGraphicCenterY(h);
  const int cx = w / 2;
  const int pad = 8;
  const int halfW = BOOT_LOGO_W / 2 + pad;
  const int halfH = BOOT_LOGO_H / 2 + pad;
  return x >= cx - halfW && x < cx + halfW && y >= cy - halfH && y < cy + halfH;
}

void showDisclaimer(SparkyTft& tft) {
  const int w = tft.width();
  const int h = tft.height();
  const bool largeUi = (w >= 700 || h >= 700);
  const int margin = largeUi ? 28 : 14;
  const int titleTs = largeUi ? 4 : 3;
  const int bodyTs = largeUi ? 2 : 1;
  const int bodyLineH = largeUi ? 26 : 16;

  tft.fillScreen(kBgDark);
  tft.setTextColor(kWhite, kBgDark);
  tft.setTextSize(titleTs);
  const char* title = "DISCLAIMER";
  int tw = (int)strlen(title) * 6 * titleTs;
  int tx = (w - tw) / 2;
  if (tx < margin) tx = margin;
  const int titleY = largeUi ? 20 : 10;
  tft.setCursor(tx, titleY);
  tft.print(title);
  tft.drawFastHLine(tx, titleY + 7 * titleTs + 5, tw, kWhite);

  char d1[80], d2[80];
  Standards_getDisclaimerStandardLines(d1, sizeof(d1), d2, sizeof(d2));
  const char* lines[] = {
    "SparkyCheck is guidance only.",
    "Not a verification/testing device.",
    "Does not replace mandatory testing.",
    d1,
    d2,
    "Responsibility remains with the user."
  };
  const int lineCount = (int)(sizeof(lines) / sizeof(lines[0]));
  tft.setTextColor(kAccentDim, kBgDark);
  tft.setTextSize(bodyTs);
  const int bodyTop = titleY + 7 * titleTs + (largeUi ? 22 : 14);
  const int bodyBottom = h - (largeUi ? 26 : 16);
  int y = bodyTop;
  for (int i = 0; i < lineCount; i++) {
    int lineW = (int)strlen(lines[i]) * 6 * bodyTs;
    int lx = (w - lineW) / 2;
    if (lx < margin) lx = margin;
    tft.setCursor(lx, y);
    tft.print(lines[i]);
    y += bodyLineH;
    if (y > bodyBottom) break;
  }

  sparkyDisplayFlush(&tft);

  // Timeout-only transition to main screen.
  const unsigned long startMs = millis();
  const unsigned long kTimeoutMs = 8000;
  while ((millis() - startMs) <= kTimeoutMs) delay(30);
}

}  // namespace BootScreen
