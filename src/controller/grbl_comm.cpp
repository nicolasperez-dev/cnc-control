#include "grbl_comm.h"
#include "config.h"
#include "state.h"
#include "gcode_streamer.h"

namespace {
HardwareSerial grblSerial(2);
char rxBuffer[256];
size_t rxLength = 0;
unsigned long lastPoll = 0;

bool parseVector(const String& text, const char* key, float& a, float& b, float& c) {
  int start = text.indexOf(key);
  if (start < 0) return false;
  start += strlen(key);
  int end = text.indexOf('|', start);
  String vector = end < 0 ? text.substring(start) : text.substring(start, end);
  return sscanf(vector.c_str(), "%f,%f,%f", &a, &b, &c) == 3;
}

void parseFeedSpindle(const String& line) {
  int start = line.indexOf("FS:");
  if (start < 0) return;
  start += 3;
  int end = line.indexOf('|', start);
  String value = end < 0 ? line.substring(start) : line.substring(start, end);
  int feed = 0, spindle = 0;
  if (sscanf(value.c_str(), "%d,%d", &feed, &spindle) >= 1) {
    sys.currentFeed = feed;
    sys.currentSpindle = spindle;
  }
}

void parseStatus(const String& line) {
  sys.grblLastLine = line;
  sys.grblConnected = true;
  sys.lastGrblResponse = millis();

  int separator = line.indexOf('|');
  int close = line.indexOf('>');
  int stateEnd = separator > 0 ? separator : close;
  if (line.startsWith("<") && stateEnd > 1) sys.grblState = line.substring(1, stateEnd);

  float x = 0, y = 0, z = 0;
  if (parseVector(line, "WPos:", x, y, z)) {
    sys.workX = x; sys.workY = y; sys.workZ = z;
    sys.haveWorkPos = true;
  }
  if (parseVector(line, "MPos:", x, y, z)) {
    sys.machineX = x; sys.machineY = y; sys.machineZ = z;
    sys.haveMachinePos = true;
  }
  if (parseVector(line, "WCO:", x, y, z)) {
    sys.wcoX = x; sys.wcoY = y; sys.wcoZ = z;
    sys.haveWco = true;
  }

  if (!sys.haveWorkPos && sys.haveMachinePos && sys.haveWco) {
    sys.workX = sys.machineX - sys.wcoX;
    sys.workY = sys.machineY - sys.wcoY;
    sys.workZ = sys.machineZ - sys.wcoZ;
  }
  parseFeedSpindle(line);
  streamerOnGrblState(sys.grblState);
}

void processLine(String line) {
  line.trim();
  if (!line.length()) return;
  ++sys.grblRxLines;
  sys.grblLastLine = line;
  sys.grblConnected = true;
  sys.lastGrblResponse = millis();

  Serial.print("[GRBL RX] ");
  Serial.println(line);

  if (line.startsWith("<")) {
    parseStatus(line);
  } else if (line == "ok") {
    streamerOnGrblOk();
  } else if (line.startsWith("error:")) {
    ++sys.grblErrors;
    sys.grblLastError = line;
    streamerOnGrblError(line);
    showToast(line, C_RED, 2500);
  } else if (line.startsWith("ALARM:")) {
    ++sys.grblErrors;
    sys.grblState = "Alarm";
    sys.grblLastError = line;
    streamerOnGrblError(line);
    showToast(line, C_RED, 3500);
  } else if (line.startsWith("Grbl ")) {
    sys.grblState = "INICIALIZADO";
  } else if (line.startsWith("[MSG:")) {
    if (line.indexOf("Reset to continue") >= 0) sys.grblState = "RESET";
  }
}

bool sendLineInternal(const String& command) {
  String clean = command;
  clean.trim();
  if (!clean.length() || clean.length() > GRBL_MAX_LINE_LEN) return false;
  grblSerial.print(clean);
  grblSerial.print('\n');
  ++sys.grblTxLines;
  Serial.print("[GRBL TX] ");
  Serial.println(clean);
  return true;
}
} // namespace

void grblInit() {
  grblSerial.setRxBufferSize(2048);
  grblSerial.begin(GRBL_BAUD, SERIAL_8N1, GRBL_RX, GRBL_TX);
  delay(150);
  grblSerial.print("\r\n\r\n");
  delay(150);
  while (grblSerial.available()) grblSerial.read();
  rxLength = 0;
  Serial.printf("[GRBL] UART2 RX=%d TX=%d baud=%d\n", GRBL_RX, GRBL_TX, GRBL_BAUD);
}

void grblLoop() {
  while (grblSerial.available()) {
    char c = (char)grblSerial.read();
    if (c == '\r' || c == '\n') {
      if (rxLength) {
        rxBuffer[rxLength] = '\0';
        processLine(String(rxBuffer));
        rxLength = 0;
      }
    } else if (rxLength < sizeof(rxBuffer) - 1) {
      rxBuffer[rxLength++] = c;
    } else {
      rxLength = 0;
      ++sys.grblErrors;
      sys.grblLastError = "Overflow de linea RX";
    }
  }

  if (millis() - lastPoll >= GRBL_STATUS_POLL_MS) {
    lastPoll = millis();
    grblRequestStatus();
  }

  if (sys.grblConnected && millis() - sys.lastGrblResponse > GRBL_TIMEOUT_MS) {
    sys.grblConnected = false;
    sys.grblState = "DESCONECTADO";
#if AUTO_ABORT_ON_GRBL_LOSS
    if (streamerIsBusy()) streamerAbortForFault("GRBL SIN RESPUESTA");
#endif
    sys.needFullRedraw = true;
  }
}

bool grblSendManualLine(const String& command) {
  if (streamerIsBusy()) {
    showToast("COMANDO BLOQUEADO DURANTE TRABAJO", C_ORANGE, 2200);
    return false;
  }
  if (!sys.grblConnected) {
    showToast("GRBL DESCONECTADO", C_RED);
    return false;
  }
  return sendLineInternal(command);
}

bool grblSendStreamerLine(const String& command) {
  return sendLineInternal(command);
}

void grblJogXY(float dx, float dy, uint16_t feed) {
  if (streamerIsBusy()) return;
  if (!feed) feed = currentJogFeed();
  feed = constrain(feed, (uint16_t)50, (uint16_t)5000);
  String command = "$J=G21G91";
  if (fabsf(dx) > 0.0001f) command += "X" + String(dx, 3);
  if (fabsf(dy) > 0.0001f) command += "Y" + String(dy, 3);
  command += "F" + String(feed);
  if (grblSendManualLine(command)) showToast("JOG XY", C_YELLOW, 500);
}

void grblJogZ(float dz, uint16_t feed) {
  if (streamerIsBusy()) return;
  if (!feed) feed = currentJogFeed();
  feed = constrain(feed, (uint16_t)50, (uint16_t)5000);
  String command = "$J=G21G91Z" + String(dz, 3) + "F" + String(feed);
  if (grblSendManualLine(command)) showToast(dz > 0 ? "Z+" : "Z-", C_YELLOW, 500);
}

void grblUnlock() { if (grblSendManualLine("$X")) showToast("DESBLOQUEAR", C_GREEN); }
void grblZeroXY() { if (grblSendManualLine("G10 L20 P1 X0 Y0")) showToast("ZERO X/Y", C_YELLOW); }
void grblZeroZ() { if (grblSendManualLine("G10 L20 P1 Z0")) showToast("ZERO Z", C_YELLOW); }

void grblRealtimeHold() {
  grblSerial.write((uint8_t)'!');
  Serial.println("[GRBL RT] HOLD");
}

void grblRealtimeResume() {
  grblSerial.write((uint8_t)'~');
  Serial.println("[GRBL RT] RESUME");
}

void grblRealtimeReset() {
  grblSerial.write((uint8_t)0x18);
  Serial.println("[GRBL RT] RESET");
}

void grblSoftStop() {
  grblRealtimeHold();
  delay(25);
  grblRealtimeReset();
  showToast("RESET GRBL", C_RED, 2000);
}

void grblRequestStatus() {
  grblSerial.write((uint8_t)'?');
}
