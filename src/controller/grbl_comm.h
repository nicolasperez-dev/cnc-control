#pragma once
#include <Arduino.h>

void grblInit();
void grblLoop();

bool grblSendManualLine(const String& command);
bool grblSendStreamerLine(const String& command);
void grblJogXY(float dx, float dy, uint16_t feed = 0);
void grblJogZ(float dz, uint16_t feed = 0);
void grblUnlock();
void grblZeroXY();
void grblZeroZ();
void grblRealtimeHold();
void grblRealtimeResume();
void grblRealtimeReset();
void grblSoftStop();
void grblRequestStatus();
