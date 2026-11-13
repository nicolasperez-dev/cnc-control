#pragma once
#include <Arduino.h>

enum StreamState : uint8_t {
  STREAM_IDLE = 0,
  STREAM_RUNNING,
  STREAM_PAUSED,
  STREAM_FINISHING,
  STREAM_COMPLETED,
  STREAM_ABORTED,
  STREAM_ERROR
};

void streamerInit();
void streamerLoop();
bool streamerStartSelected();
bool streamerStartDirect(const String& name, uint32_t totalBytes);
bool streamerDirectAcceptLine(const String& rawLine, uint32_t sourceBytes);
bool streamerDirectTakeAck();
bool streamerDirectFinishInput();
bool streamerDirectMode();
void streamerDirectAbort(const String& reason);
void streamerDirectRelease();
void streamerPause();
void streamerResume();
void streamerStop();
void streamerAbortForFault(const String& reason);
void streamerOnGrblOk();
void streamerOnGrblError(const String& error);
void streamerOnGrblState(const String& state);

StreamState streamerState();
const char* streamerStateText();
bool streamerIsBusy();
bool streamerIsRunning();
bool streamerIsPaused();
bool streamerUsesSd();
uint8_t streamerProgressPercent();
uint32_t streamerCurrentLine();
uint32_t streamerAckedLine();
uint32_t streamerBytesRead();
uint32_t streamerFileSize();
const String& streamerFileName();
const String& streamerLastCommand();
const String& streamerError();
