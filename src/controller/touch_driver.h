#pragma once
#include <Arduino.h>
#include "TFT_eSPI.h"

struct TouchEvent {
  bool down = false;
  bool justPressed = false;
  bool justReleased = false;
  bool tap = false;
  int16_t x = 0;
  int16_t y = 0;
  uint32_t heldMs = 0;
};

void touchInit();
TouchEvent touchPoll();
bool touchCalibrationValid();
bool touchRunCalibration(TFT_eSPI& tft);
void touchRunCoordinateTest(TFT_eSPI& tft, uint32_t timeoutMs = 15000);
void touchClearCalibration();
bool touchBootCalibrationRequested(uint32_t holdMs = 2200);
