#if defined(SPARKYCHECK_PANEL_43B)

#include "SparkyPanelDisplay.h"

#include <Arduino.h>
#include <esp_display_panel.hpp>

using namespace esp_panel::board;
using namespace esp_panel::drivers;

static Board* s_board = nullptr;

SparkyPanelDisplay::SparkyPanelDisplay() : Adafruit_GFX(800, 480) {}

bool SparkyPanelDisplay::setupBoard() {
  if (s_board) {
    lcd_ = s_board->getLCD();
    touch_ = s_board->getTouch();
    return lcd_ != nullptr;
  }
  s_board = new Board();
  s_board->init();
  if (!s_board->begin()) {
    return false;
  }
  lcd_ = s_board->getLCD();
  touch_ = s_board->getTouch();
  if (!lcd_) {
    return false;
  }
  fb_ = static_cast<uint16_t*>(lcd_->getFrameBufferByIndex(0));
  fbW_ = lcd_->getFrameWidth();
  fbH_ = lcd_->getFrameHeight();
  return fb_ != nullptr && fbW_ > 0 && fbH_ > 0;
}

void SparkyPanelDisplay::init() {
  if (!setupBoard() || !fb_) {
    Serial.println("SparkyCheck: Waveshare panel init failed (ESP32_Display_Panel).");
    while (true) {
      delay(500);
    }
  }
}

void SparkyPanelDisplay::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (!fb_) {
    return;
  }
  if (x < 0 || y < 0 || x >= _width || y >= _height) {
    return;
  }
  int16_t t;
  int16_t X = x;
  int16_t Y = y;
  switch (rotation) {
    case 1:
      t = X;
      X = WIDTH - 1 - Y;
      Y = t;
      break;
    case 2:
      X = WIDTH - 1 - X;
      Y = HEIGHT - 1 - Y;
      break;
    case 3:
      t = X;
      X = Y;
      Y = HEIGHT - 1 - t;
      break;
    default:
      break;
  }
  if (X < 0 || Y < 0 || X >= fbW_ || Y >= fbH_) {
    return;
  }
  fb_[Y * fbW_ + X] = color;
}

void SparkyPanelDisplay::flush() {
  if (!lcd_ || !fb_) {
    return;
  }
  lcd_->drawBitmap(0, 0, fbW_, fbH_, reinterpret_cast<uint8_t*>(fb_), 0);
}

/** Map native panel coords (same space as GFXcanvas16 after drawPixel) to logical (_width x _height). */
static void nativeToLogical(uint8_t rotation, int16_t WIDTH, int16_t HEIGHT, int16_t nx, int16_t ny,
                            int16_t* lx, int16_t* ly) {
  int16_t t;
  switch (rotation) {
    case 0:
      *lx = nx;
      *ly = ny;
      break;
    case 1:
      *lx = ny;
      *ly = WIDTH - 1 - nx;
      break;
    case 2:
      *lx = WIDTH - 1 - nx;
      *ly = HEIGHT - 1 - ny;
      break;
    case 3:
      t = nx;
      *lx = HEIGHT - 1 - ny;
      *ly = t;
      break;
    default:
      *lx = nx;
      *ly = ny;
      break;
  }
}

bool SparkyPanelDisplay::getTouch(uint16_t* x, uint16_t* y) {
  if (!touch_ || !x || !y) {
    return false;
  }
  TouchPoint pts[2];
  int n = touch_->readPoints(pts, 1, 10);
  if (n < 1 || pts[0].strength <= 0) {
    return false;
  }
  int16_t lx = 0;
  int16_t ly = 0;
  nativeToLogical(getRotation(), WIDTH, HEIGHT, pts[0].x, pts[0].y, &lx, &ly);
  if (lx < 0) {
    lx = 0;
  }
  if (ly < 0) {
    ly = 0;
  }
  *x = (uint16_t)lx;
  *y = (uint16_t)ly;
  return true;
}

#endif  // SPARKYCHECK_PANEL_43B
