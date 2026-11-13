#pragma once
#include <Arduino.h>
#include "touch_driver.h"

void uiInit();
void uiLoop();
void uiHandleTouch(const TouchEvent& event);
void uiForceRedraw();
