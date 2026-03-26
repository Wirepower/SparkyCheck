#pragma once

#include <stddef.h>
#include <stdint.h>
#include "Screens.h"

typedef struct {
  int16_t x;
  int16_t y;
  const char* text;
} EezMockupLabel;

typedef struct {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
  const char* text;
  ScreenId target;
} EezMockupButton;

typedef struct {
  ScreenId id;
  const char* name;
  const EezMockupLabel* labels;
  size_t labelCount;
  const EezMockupButton* buttons;
  size_t buttonCount;
} EezMockupScreen;

const EezMockupScreen* EezMockupData_findScreen(ScreenId id);
