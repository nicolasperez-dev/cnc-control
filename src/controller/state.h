#pragma once
#include <Arduino.h>
#include "config.h"

enum UiPage : uint8_t {
  PAGE_DASHBOARD = 0,
  PAGE_MANUAL,
  PAGE_FILES,
  PAGE_NETWORK
};

struct SystemState {
  UiPage page = PAGE_DASHBOARD;
  bool needFullRedraw = true;

  // GRBL
  bool grblConnected = false;
  String grblState = "DESCONECTADO";
  String grblLastLine = "";
  String grblLastError = "";
  unsigned long lastGrblResponse = 0;
  float workX = 0.0f;
  float workY = 0.0f;
  float workZ = 0.0f;
  float machineX = 0.0f;
  float machineY = 0.0f;
  float machineZ = 0.0f;
  float wcoX = 0.0f;
  float wcoY = 0.0f;
  float wcoZ = 0.0f;
  bool haveWorkPos = false;
  bool haveMachinePos = false;
  bool haveWco = false;
  int currentFeed = 0;
  int currentSpindle = 0;
  uint32_t grblRxLines = 0;
  uint32_t grblTxLines = 0;
  uint32_t grblErrors = 0;

  // UI / jog
  uint8_t stepIndex = 1; // 1 mm
  uint8_t jogFeedIndex = DEFAULT_JOG_FEED_INDEX;
  int fileListOffset = 0;

  // SD
  bool sdMounted = false;
  String sdError = "";

  // Red
  bool wifiConnected = false;
  bool wifiConnecting = false;
  String wifiSsid = "";
  String wifiIp = "";
  String apSsid = "";
  String apIp = "192.168.4.1";
  bool btReady = false;
  bool btConnected = false;
  bool otaInProgress = false;
  uint8_t otaProgress = 0;

  // Mensajes temporales
  String toast = "";
  uint16_t toastColor = C_YELLOW;
  unsigned long toastUntil = 0;
};

extern SystemState sys;

void stateInit();
float currentStep();
void setStepIndex(uint8_t index);
uint16_t currentJogFeed();
void setJogFeedIndex(uint8_t index);
void showToast(const String& message, uint16_t color = C_YELLOW, uint32_t durationMs = 1600);
void stateLoop();
const char* boolText(bool value);
