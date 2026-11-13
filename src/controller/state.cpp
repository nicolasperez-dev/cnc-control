#include "state.h"

SystemState sys;

void stateInit() {
  sys.grblState.reserve(20);
  sys.grblLastLine.reserve(180);
  sys.grblLastError.reserve(100);
  sys.wifiSsid.reserve(40);
  sys.wifiIp.reserve(20);
  sys.apSsid.reserve(40);
  sys.apIp.reserve(20);
  sys.sdError.reserve(80);
  sys.toast.reserve(60);
}

float currentStep() {
  if (sys.stepIndex >= STEP_COUNT) sys.stepIndex = 1;
  return STEP_VALUES[sys.stepIndex];
}

void setStepIndex(uint8_t index) {
  if (index >= STEP_COUNT) return;
  sys.stepIndex = index;
  sys.needFullRedraw = true;
}

uint16_t currentJogFeed() {
  if (sys.jogFeedIndex >= JOG_FEED_COUNT) sys.jogFeedIndex = DEFAULT_JOG_FEED_INDEX;
  return JOG_FEED_VALUES[sys.jogFeedIndex];
}

void setJogFeedIndex(uint8_t index) {
  if (index >= JOG_FEED_COUNT) return;
  sys.jogFeedIndex = index;
  sys.needFullRedraw = true;
}

void showToast(const String& message, uint16_t color, uint32_t durationMs) {
  sys.toast = message;
  sys.toastColor = color;
  sys.toastUntil = millis() + durationMs;
}

void stateLoop() {
  if (sys.toast.length() && (int32_t)(millis() - sys.toastUntil) >= 0) {
    sys.toast = "";
    sys.needFullRedraw = true;
  }
}

const char* boolText(bool value) {
  return value ? "SI" : "NO";
}
