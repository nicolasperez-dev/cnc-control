#include "gcode_streamer.h"
#include "config.h"
#include "state.h"
#include "sd_manager.h"
#include "grbl_comm.h"
#include <FS.h>

namespace {
fs::File jobFile;
StreamState state = STREAM_IDLE;
bool waitingAck = false;
bool reachedEof = false;
bool directMode = false;
bool directAckReady = false;
uint32_t currentLine = 0;
uint32_t ackedLine = 0;
uint32_t bytesRead = 0;
uint32_t fileSize = 0;
uint32_t finishingSince = 0;
String fileName;
String lastCommand;
String lastError;

void closeJob() {
  if (jobFile) jobFile.close();
  waitingAck = false;
}

String stripCommentsAndWhitespace(String line) {
  line.replace("\r", "");
  int semicolon = line.indexOf(';');
  if (semicolon >= 0) line.remove(semicolon);

  // Elimina comentarios entre parentesis, incluso varios en la misma linea.
  int open = line.indexOf('(');
  while (open >= 0) {
    int close = line.indexOf(')', open + 1);
    if (close < 0) {
      line.remove(open);
      break;
    }
    line.remove(open, close - open + 1);
    open = line.indexOf('(');
  }

  line.trim();
  if (line == "%") line = "";
  return line;
}

bool readNextExecutableLine(String& output) {
  output = "";
  int skipped = 0;
  while (jobFile && jobFile.available()) {
    String raw = jobFile.readStringUntil('\n');
    bytesRead = (uint32_t)jobFile.position();
    ++currentLine;
    output = stripCommentsAndWhitespace(raw);
    if (output.length()) return true;
    if (++skipped >= 30) return false; // cede tiempo al resto del sistema
  }
  if (jobFile && !jobFile.available()) reachedEof = true;
  return false;
}

void setError(const String& reason, bool resetGrbl) {
  lastError = reason;
  state = STREAM_ERROR;
  directAckReady = false;
  closeJob();
  if (resetGrbl) {
    grblRealtimeHold();
    delay(20);
    grblRealtimeReset();
  }
  showToast(reason, C_RED, 3500);
  sys.needFullRedraw = true;
}
} // namespace

void streamerInit() {
  fileName.reserve(64);
  lastCommand.reserve(96);
  lastError.reserve(120);
}

bool streamerStartSelected() {
  if (streamerIsBusy()) {
    showToast("YA HAY UN TRABAJO ACTIVO", C_ORANGE);
    return false;
  }
  if (!sdIsMounted()) {
    showToast("MICROSD NO DISPONIBLE", C_RED);
    return false;
  }
  if (!sys.grblConnected) {
    showToast("GRBL DESCONECTADO", C_RED);
    return false;
  }
#if REQUIRE_GRBL_IDLE_TO_START
  if (!sys.grblState.startsWith("Idle")) {
    showToast("GRBL DEBE ESTAR EN IDLE", C_ORANGE, 2400);
    return false;
  }
#endif
  if (!sdSelectedName().length()) {
    showToast("SELECCIONA UN ARCHIVO", C_ORANGE);
    return false;
  }

  jobFile = sdOpenSelected(FILE_READ);
  if (!jobFile) {
    showToast("NO SE PUDO ABRIR EL ARCHIVO", C_RED);
    return false;
  }

  fileName = sdSelectedName();
  directMode = false;
  directAckReady = false;
  fileSize = (uint32_t)jobFile.size();
  currentLine = 0;
  ackedLine = 0;
  bytesRead = 0;
  waitingAck = false;
  reachedEof = false;
  lastCommand = "";
  lastError = "";
  finishingSince = 0;
  state = STREAM_RUNNING;
  jobFile.setTimeout(20);
  showToast("INICIANDO " + fileName, C_GREEN, 1800);
  sys.needFullRedraw = true;
  Serial.printf("[STREAM] inicio: %s (%lu bytes)\n", fileName.c_str(),
                static_cast<unsigned long>(fileSize));
  return true;
}

bool streamerStartDirect(const String& name, uint32_t totalBytes) {
  if (streamerIsBusy()) {
    lastError = "Ya hay un trabajo activo";
    return false;
  }
  if (!sys.grblConnected) {
    lastError = "GRBL desconectado";
    return false;
  }
#if REQUIRE_GRBL_IDLE_TO_START
  if (!sys.grblState.startsWith("Idle")) {
    lastError = "GRBL debe estar en Idle";
    return false;
  }
#endif
  closeJob();
  fileName = name.length() ? name : "CORTE_DIRECTO";
  fileSize = totalBytes;
  currentLine = 0;
  ackedLine = 0;
  bytesRead = 0;
  waitingAck = false;
  reachedEof = false;
  directMode = true;
  directAckReady = false;
  lastCommand = "";
  lastError = "";
  finishingSince = 0;
  state = STREAM_RUNNING;
  showToast("CORTE DIRECTO DESDE WEB", C_GREEN, 2200);
  sys.page = PAGE_DASHBOARD;
  sys.needFullRedraw = true;
  Serial.printf("[DIRECT] inicio: %s (%lu bytes)\n", fileName.c_str(), (unsigned long)fileSize);
  return true;
}

bool streamerDirectAcceptLine(const String& rawLine, uint32_t sourceBytes) {
  if (!directMode || state != STREAM_RUNNING || waitingAck || directAckReady) return false;
  bytesRead = min(fileSize, bytesRead + sourceBytes);
  ++currentLine;
  String line = stripCommentsAndWhitespace(rawLine);
  if (!line.length()) {
    ackedLine = currentLine;
    directAckReady = true;
    return true;
  }
  if (line.length() > GRBL_MAX_LINE_LEN) {
    setError("LINEA " + String(currentLine) + " SUPERA " + String(GRBL_MAX_LINE_LEN) + " CARACTERES", true);
    return false;
  }
  lastCommand = line;
  if (!grblSendStreamerLine(line)) {
    setError("NO SE PUDO ENVIAR LA LINEA " + String(currentLine), false);
    return false;
  }
  waitingAck = true;
  return true;
}

bool streamerDirectTakeAck() {
  if (!directMode || !directAckReady || state != STREAM_RUNNING) return false;
  directAckReady = false;
  return true;
}

bool streamerDirectFinishInput() {
  if (!directMode || state != STREAM_RUNNING || waitingAck || directAckReady) return false;
  reachedEof = true;
  state = STREAM_FINISHING;
  finishingSince = millis();
  sys.needFullRedraw = true;
  return true;
}

bool streamerDirectMode() { return directMode; }

void streamerDirectAbort(const String& reason) {
  if (!directMode || !streamerIsBusy()) return;
  setError(reason, true);
}

void streamerDirectRelease() {
  directMode = false;
  directAckReady = false;
}

void streamerLoop() {
  if (state != STREAM_RUNNING) return;
  if (directMode) return; // Las lineas llegan desde network_manager.
  if (!jobFile) {
    setError("ARCHIVO SD CERRADO INESPERADAMENTE", true);
    return;
  }
  if (!sys.grblConnected) {
#if AUTO_ABORT_ON_GRBL_LOSS
    setError("SE PERDIO LA COMUNICACION CON GRBL", false);
#endif
    return;
  }
  if (waitingAck) return;

  if (reachedEof) {
    state = STREAM_FINISHING;
    finishingSince = millis();
    closeJob();
    sys.needFullRedraw = true;
    return;
  }

  String line;
  if (!readNextExecutableLine(line)) {
    if (reachedEof) {
      state = STREAM_FINISHING;
      finishingSince = millis();
      closeJob();
      sys.needFullRedraw = true;
    }
    return;
  }

  if (line.length() > GRBL_MAX_LINE_LEN) {
    setError("LINEA " + String(currentLine) + " SUPERA " + String(GRBL_MAX_LINE_LEN) + " CARACTERES", true);
    return;
  }

  lastCommand = line;
  if (!grblSendStreamerLine(line)) {
    setError("NO SE PUDO ENVIAR LA LINEA " + String(currentLine), false);
    return;
  }
  waitingAck = true;
}

void streamerPause() {
  if (state != STREAM_RUNNING && state != STREAM_FINISHING) return;
  grblRealtimeHold();
  state = STREAM_PAUSED;
  showToast("TRABAJO PAUSADO", C_ORANGE);
  sys.needFullRedraw = true;
}

void streamerResume() {
  if (state != STREAM_PAUSED) return;
  grblRealtimeResume();
  state = reachedEof ? STREAM_FINISHING : STREAM_RUNNING;
  if (state == STREAM_FINISHING && finishingSince == 0) finishingSince = millis();
  showToast("TRABAJO REANUDADO", C_GREEN);
  sys.needFullRedraw = true;
}

void streamerStop() {
  if (!streamerIsBusy()) return;
  grblRealtimeHold();
  delay(25);
  grblRealtimeReset();
  closeJob();
  state = STREAM_ABORTED;
  directAckReady = false;
  lastError = "Detenido por el operador";
  showToast("TRABAJO DETENIDO", C_RED, 2500);
  sys.needFullRedraw = true;
}

void streamerAbortForFault(const String& reason) {
  if (!streamerIsBusy()) return;
  setError(reason, true);
}

void streamerOnGrblOk() {
  if (!waitingAck) return;
  waitingAck = false;
  ackedLine = currentLine;
  if (directMode) {
    directAckReady = true;
    return;
  }
  if (reachedEof && state == STREAM_RUNNING) {
    state = STREAM_FINISHING;
    finishingSince = millis();
    closeJob();
  }
}

void streamerOnGrblError(const String& error) {
  if (!waitingAck && !streamerIsBusy()) return;
  waitingAck = false;
  setError("GRBL: " + error + " EN LINEA " + String(currentLine), true);
}

void streamerOnGrblState(const String& machineState) {
  if (state == STREAM_FINISHING && machineState.startsWith("Idle")) {
    if (finishingSince == 0) finishingSince = millis();
    if (millis() - finishingSince > 400) {
      state = STREAM_COMPLETED;
      bytesRead = fileSize;
      showToast("TRABAJO COMPLETADO", C_GREEN, 3000);
      sys.needFullRedraw = true;
      Serial.printf("[STREAM] completado: %s\n", fileName.c_str());
    }
  }
}

StreamState streamerState() { return state; }

const char* streamerStateText() {
  switch (state) {
    case STREAM_IDLE: return "LISTO";
    case STREAM_RUNNING: return "EJECUTANDO";
    case STREAM_PAUSED: return "PAUSADO";
    case STREAM_FINISHING: return "FINALIZANDO";
    case STREAM_COMPLETED: return "COMPLETADO";
    case STREAM_ABORTED: return "DETENIDO";
    case STREAM_ERROR: return "ERROR";
    default: return "--";
  }
}

bool streamerIsBusy() {
  return state == STREAM_RUNNING || state == STREAM_PAUSED || state == STREAM_FINISHING;
}

bool streamerIsRunning() { return state == STREAM_RUNNING || state == STREAM_FINISHING; }
bool streamerIsPaused() { return state == STREAM_PAUSED; }
bool streamerUsesSd() { return streamerIsBusy() && !directMode; }

uint8_t streamerProgressPercent() {
  if (state == STREAM_COMPLETED) return 100;
  if (fileSize == 0) return 0;
  uint32_t value = (uint32_t)((uint64_t)bytesRead * 100ULL / fileSize);
  return (uint8_t)min((uint32_t)100, value);
}

uint32_t streamerCurrentLine() { return currentLine; }
uint32_t streamerAckedLine() { return ackedLine; }
uint32_t streamerBytesRead() { return bytesRead; }
uint32_t streamerFileSize() { return fileSize; }
const String& streamerFileName() { return fileName; }
const String& streamerLastCommand() { return lastCommand; }
const String& streamerError() { return lastError; }
