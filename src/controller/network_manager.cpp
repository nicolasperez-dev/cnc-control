#include "network_manager.h"
#include "config.h"
#include "state.h"
#include "grbl_comm.h"
#include "gcode_streamer.h"
#include "sd_manager.h"

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <SD.h>

namespace {
WebServer internalServer(ESP32_INTERNAL_HTTP_PORT);
WiFiServer fileServer(ESP32_FILE_PORT);

IPAddress esp32Ip(192, 168, 4, 2);
IPAddress gatewayIp(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

bool routesReady = false;
bool serversStarted = false;
bool calibrationRequested = false;
unsigned long lastConnectAttempt = 0;
String lastGatewayEvent = "ESPERANDO ESP8266";
String gatewayExternalIp = "--";
unsigned long lastGatewayInfoPoll = 0;

enum UploadState : uint8_t {
  UPLOAD_IDLE = 0,
  UPLOAD_COMMAND,
  UPLOAD_LENGTH,
  UPLOAD_DATA,
  UPLOAD_DIRECT_WAIT_ACK,
  UPLOAD_DIRECT_WAIT_FINISH
};

enum TransferMode : uint8_t {
  TRANSFER_STORE = 0,
  TRANSFER_DIRECT
};

WiFiClient uploadClient;
fs::File uploadFile;
UploadState uploadState = UPLOAD_IDLE;
String uploadCommand;
String uploadName;
String uploadPath;
String uploadFinalPath;
String directLine;
TransferMode transferMode = TRANSFER_STORE;
uint32_t directFrameLength = 0;
uint8_t lengthBytes[4] = {0};
uint8_t lengthIndex = 0;
uint32_t frameRemaining = 0;
uint32_t uploadBytes = 0;
unsigned long uploadLastActivity = 0;
uint8_t transferBuffer[1024];

String jsonEscape(const String& input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input.charAt(i);
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if ((uint8_t)c >= 32) out += c;
  }
  return out;
}

void sendJson(int code, const String& json) {
  internalServer.sendHeader("Cache-Control", "no-store");
  internalServer.sendHeader("Connection", "close");
  internalServer.send(code, "application/json; charset=utf-8", json);
}

void sendError(int code, const String& error) {
  sendJson(code, "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}");
}

void sendStatus() {
  String json;
  json.reserve(600);
  json += "{";
  json += "\"ok\":true,";
  json += "\"role\":\"esp32-cnc\",";
  json += "\"version\":\"" FW_VERSION "\",";
  json += "\"gateway\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"grblConnected\":" + String(sys.grblConnected ? "true" : "false") + ",";
  json += "\"grbl\":\"" + jsonEscape(sys.grblState) + "\",";
  json += "\"x\":" + String(sys.workX, 3) + ",";
  json += "\"y\":" + String(sys.workY, 3) + ",";
  json += "\"z\":" + String(sys.workZ, 3) + ",";
  json += "\"jogFeed\":" + String(currentJogFeed()) + ",";
  json += "\"job\":\"" + String(streamerStateText()) + "\",";
  json += "\"progress\":" + String(streamerProgressPercent()) + ",";
  json += "\"line\":" + String(streamerCurrentLine()) + ",";
  String currentFile = streamerFileName().length() ? streamerFileName() : sdSelectedName();
  json += "\"file\":\"" + jsonEscape(currentFile) + "\",";
  json += "\"sd\":" + String(sdIsMounted() ? "true" : "false") + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"lastEvent\":\"" + jsonEscape(lastGatewayEvent) + "\"";
  json += "}";
  sendJson(200, json);
}

void sendFiles() {
  String json = "{\"ok\":true,\"files\":[";
  for (int i = 0; i < sdFileCount(); ++i) {
    const SdFileInfo* file = sdFileAt(i);
    if (!file) continue;
    if (i) json += ',';
    json += "{\"index\":" + String(i) + ",\"name\":\"" + jsonEscape(file->name) +
            "\",\"size\":" + String(file->size) +
            ",\"selected\":" + String(i == sdSelectedIndex() ? "true" : "false") + "}";
  }
  json += "]}";
  sendJson(200, json);
}

bool actionAllowedWithoutGrbl(const String& action) {
  return action == "refresh" || action == "calibrate" || action == "stop";
}

bool performAction(const String& action, String& error) {
  if (!actionAllowedWithoutGrbl(action) && !sys.grblConnected) {
    error = "GRBL desconectado";
    return false;
  }
  if (action == "start") {
    if (!streamerStartSelected()) { error = "No se pudo iniciar el archivo"; return false; }
  } else if (action == "pause") {
    if (streamerIsBusy()) streamerPause();
    else grblRealtimeHold();
  } else if (action == "hold") grblRealtimeHold();
  else if (action == "resume") {
    if (streamerIsBusy()) streamerResume();
    else grblRealtimeResume();
  }
  else if (action == "stop") streamerStop();
  else if (action == "unlock") grblUnlock();
  else if (action == "zeroxy") grblZeroXY();
  else if (action == "zeroz") grblZeroZ();
  else if (action == "reset") grblRealtimeReset();
  else if (action == "refresh") sdRefreshFiles();
  else if (action == "calibrate") {
    if (streamerIsBusy()) { error = "Calibracion bloqueada durante un trabajo"; return false; }
    calibrationRequested = true;
  } else {
    error = "Accion desconocida";
    return false;
  }
  return true;
}

void configureRoutes() {
  if (routesReady) return;
  routesReady = true;

  internalServer.on("/ping", HTTP_GET, []() {
    lastGatewayEvent = "PING ESP8266";
    sendJson(200, "{\"ok\":true,\"role\":\"esp32-cnc\",\"version\":\"" FW_VERSION "\"}");
  });

  internalServer.on("/status", HTTP_GET, []() {
    lastGatewayEvent = "ESTADO SOLICITADO";
    sendStatus();
  });

  internalServer.on("/files", HTTP_GET, []() {
    lastGatewayEvent = "ARCHIVOS SOLICITADOS";
    sendFiles();
  });

  internalServer.on("/select", HTTP_POST, []() {
    int index = internalServer.arg("index").toInt();
    if (!sdSelectIndex(index)) { sendError(404, "Archivo inexistente"); return; }
    sys.needFullRedraw = true;
    lastGatewayEvent = "ARCHIVO SELECCIONADO: " + sdSelectedName();
    sendJson(200, "{\"ok\":true,\"name\":\"" + jsonEscape(sdSelectedName()) + "\"}");
  });

  internalServer.on("/delete", HTTP_POST, []() {
    if (streamerIsBusy()) { sendError(409, "Trabajo activo"); return; }
    int index = internalServer.arg("index").toInt();
    if (!sdSelectIndex(index)) { sendError(404, "Archivo inexistente"); return; }
    String name = sdSelectedName();
    if (!sdDeleteSelected()) { sendError(500, "No se pudo eliminar"); return; }
    lastGatewayEvent = "ARCHIVO ELIMINADO: " + name;
    sys.needFullRedraw = true;
    sendJson(200, "{\"ok\":true}");
  });

  internalServer.on("/action", HTTP_POST, []() {
    String action = internalServer.arg("name");
    action.trim();
    String error;
    if (!performAction(action, error)) { sendError(409, error); return; }
    lastGatewayEvent = "ACCION: " + action;
    sys.needFullRedraw = true;
    sendJson(200, "{\"ok\":true,\"action\":\"" + jsonEscape(action) + "\"}");
  });

  internalServer.on("/jog", HTTP_POST, []() {
    if (streamerIsBusy()) { sendError(409, "Trabajo activo"); return; }
    if (!sys.grblConnected) { sendError(409, "GRBL desconectado"); return; }
    float dx = internalServer.arg("dx").toFloat();
    float dy = internalServer.arg("dy").toFloat();
    float dz = internalServer.arg("dz").toFloat();
    int requestedFeed = internalServer.arg("feed").toInt();
    uint16_t feed = requestedFeed > 0
                      ? (uint16_t)constrain(requestedFeed, 50, 5000)
                      : currentJogFeed();
    if (fabsf(dx) < 0.0001f && fabsf(dy) < 0.0001f && fabsf(dz) < 0.0001f) {
      sendError(400, "Movimiento vacio");
      return;
    }
    if (fabsf(dz) > 0.0001f) grblJogZ(dz, feed);
    else grblJogXY(dx, dy, feed);
    lastGatewayEvent = "JOG DESDE ESP8266";
    sendJson(200, "{\"ok\":true}");
  });

  internalServer.on("/command", HTTP_POST, []() {
    if (streamerIsBusy()) { sendError(409, "Trabajo activo"); return; }
    if (!sys.grblConnected) { sendError(409, "GRBL desconectado"); return; }
    String command = internalServer.arg("line");
    command.trim();
    String upper = command;
    upper.toUpperCase();
    if (!command.length() || command.length() > GRBL_MAX_LINE_LEN ||
        command.indexOf('\n') >= 0 || command.indexOf('\r') >= 0) {
      sendError(400, "Comando vacio o demasiado largo");
      return;
    }
    if (upper == "$H" || upper.startsWith("$H=")) {
      sendError(403, "HOME bloqueado: esta CNC no usa finales de carrera");
      return;
    }
    if (!grblSendManualLine(command)) { sendError(409, "GRBL no acepto el comando"); return; }
    lastGatewayEvent = "COMANDO MANUAL: " + command;
    sendJson(200, "{\"ok\":true}");
  });

  internalServer.onNotFound([]() { sendError(404, "Ruta interna inexistente"); });
}

void startServers() {
  if (serversStarted) return;
  configureRoutes();
  internalServer.begin();
  fileServer.begin();
  fileServer.setNoDelay(true);
  serversStarted = true;
  Serial.printf("[GATEWAY] API interna http://%s:%u | archivos TCP %u\n",
                WiFi.localIP().toString().c_str(), ESP32_INTERNAL_HTTP_PORT, ESP32_FILE_PORT);
}

void replyUpload(const String& message) {
  if (uploadClient && uploadClient.connected()) {
    uploadClient.print(message);
    uploadClient.print('\n');
    uploadClient.flush();
  }
}

void resetUpload(bool removePartial) {
  if (uploadFile) uploadFile.close();
  if (removePartial && transferMode == TRANSFER_STORE && uploadPath.length()) SD.remove(uploadPath);
  if (uploadClient) uploadClient.stop();
  if (transferMode == TRANSFER_DIRECT) streamerDirectRelease();
  uploadState = UPLOAD_IDLE;
  transferMode = TRANSFER_STORE;
  uploadCommand = "";
  uploadName = "";
  uploadPath = "";
  uploadFinalPath = "";
  directLine = "";
  directFrameLength = 0;
  lengthIndex = 0;
  frameRemaining = 0;
  uploadBytes = 0;
}

void failUpload(const String& error) {
  Serial.printf("[GATEWAY FILE] ERROR: %s\n", error.c_str());
  lastGatewayEvent = "ERROR ARCHIVO: " + error;
  if (transferMode == TRANSFER_DIRECT && streamerDirectMode() && streamerIsBusy()) {
    streamerDirectAbort(error);
  }
  replyUpload("ERR|" + error);
  showToast("ERROR RECIBIENDO ARCHIVO", C_RED, 2800);
  delay(35);
  resetUpload(true);
}

bool openUploadFromCommand() {
  uploadCommand.trim();
  const bool store = uploadCommand.startsWith("PUT ");
  const bool direct = uploadCommand.startsWith("RUN ");
  if (!store && !direct) { failUpload("PROTOCOLO INVALIDO"); return false; }
  if (streamerIsBusy()) { failUpload("MAQUINA OCUPADA"); return false; }

  uint32_t declaredSize = 0;
  String namePart;
  if (store) {
    transferMode = TRANSFER_STORE;
    namePart = uploadCommand.substring(4);
  } else {
    transferMode = TRANSFER_DIRECT;
    String spec = uploadCommand.substring(4);
    int separator = spec.lastIndexOf(' ');
    if (separator <= 0) { failUpload("RUN REQUIERE NOMBRE Y TAMANO"); return false; }
    namePart = spec.substring(0, separator);
    declaredSize = (uint32_t)strtoul(spec.substring(separator + 1).c_str(), nullptr, 10);
    if (!declaredSize || declaredSize > SD_MAX_FILE_BYTES) {
      failUpload("TAMANO DE CORTE DIRECTO INVALIDO");
      return false;
    }
  }

  uploadName = sdSanitizeUploadName(namePart);
  if (!sdIsAllowedGcodeName(uploadName)) { failUpload("NOMBRE O EXTENSION INVALIDA"); return false; }

  if (store) {
    if (!sdIsMounted()) { failUpload("SD NO DISPONIBLE"); return false; }
    uploadFinalPath = sdNormalizePath(uploadName);
    uploadPath = "/." + uploadName + ".upload";
    SD.remove(uploadPath);
    uploadFile = SD.open(uploadPath, FILE_WRITE);
    if (!uploadFile) { failUpload("NO SE PUDO CREAR EL TEMPORAL EN SD"); return false; }
  } else if (!streamerStartDirect(uploadName, declaredSize)) {
    failUpload(streamerError().length() ? streamerError() : "NO SE PUDO INICIAR CORTE DIRECTO");
    return false;
  }

  uploadBytes = 0;
  lengthIndex = 0;
  uploadState = UPLOAD_LENGTH;
  lastGatewayEvent = String(store ? "RECIBIENDO SD: " : "CORTE DIRECTO: ") + uploadName;
  if (store) {
    showToast("RECIBIENDO G-CODE DEL 8266", C_YELLOW, 2400);
    sys.page = PAGE_FILES;
  }
  sys.needFullRedraw = true;
  replyUpload(String("READY|") + (store ? "PUT|" : "RUN|") + uploadName);
  Serial.printf("[GATEWAY FILE] %s %s\n", store ? "PUT" : "RUN", uploadName.c_str());
  return true;
}

void finishUpload() {
  if (uploadFile) uploadFile.close();
  SD.remove(uploadFinalPath);
  if (!SD.rename(uploadPath, uploadFinalPath)) {
    failUpload("NO SE PUDO FINALIZAR EL ARCHIVO EN SD");
    return;
  }
  uploadPath = "";
  sdRefreshFiles();
  sdSelectByName(uploadName);
  lastGatewayEvent = "ARCHIVO OK: " + uploadName;
  showToast("G-CODE GUARDADO EN SD", C_GREEN, 2800);
  sys.needFullRedraw = true;
  replyUpload("OK|" + uploadName + "|" + String(uploadBytes));
  Serial.printf("[GATEWAY FILE] OK %s %lu bytes\n", uploadName.c_str(), (unsigned long)uploadBytes);
  delay(20);
  resetUpload(false);
}

void serviceFileTransfer() {
  if (uploadState == UPLOAD_IDLE) {
    WiFiClient incoming = fileServer.available();
    if (!incoming) return;
    uploadClient = incoming;
    uploadClient.setNoDelay(true);
    uploadState = UPLOAD_COMMAND;
    uploadCommand = "";
    uploadLastActivity = millis();
  }

  if (!uploadClient.connected() && !uploadClient.available()) {
    failUpload("CONEXION CORTADA");
    return;
  }

  if (uploadState == UPLOAD_DIRECT_WAIT_ACK) {
    if (streamerDirectTakeAck()) {
      replyUpload("NEXT|" + String(streamerAckedLine()) + "|" + String(streamerBytesRead()));
      uploadState = UPLOAD_LENGTH;
      uploadLastActivity = millis();
    } else if (streamerState() == STREAM_ERROR || streamerState() == STREAM_ABORTED) {
      failUpload(streamerError().length() ? streamerError() : "CORTE DIRECTO DETENIDO");
    }
    return;
  }

  if (uploadState == UPLOAD_DIRECT_WAIT_FINISH) {
    if (streamerState() == STREAM_COMPLETED) {
      lastGatewayEvent = "CORTE DIRECTO COMPLETADO: " + uploadName;
      replyUpload("OK|DIRECT|" + uploadName + "|" + String(uploadBytes));
      delay(20);
      resetUpload(false);
    } else if (streamerState() == STREAM_ERROR || streamerState() == STREAM_ABORTED) {
      failUpload(streamerError().length() ? streamerError() : "CORTE DIRECTO DETENIDO");
    }
    return;
  }

  if (transferMode == TRANSFER_DIRECT && streamerIsPaused()) return;

  if (millis() - uploadLastActivity > ESP32_FILE_TIMEOUT_MS) {
    failUpload("TIEMPO AGOTADO");
    return;
  }

  int budget = 4096;
  while (uploadClient.available() && budget > 0) {
    uploadLastActivity = millis();
    if (uploadState == UPLOAD_COMMAND) {
      char c = (char)uploadClient.read();
      --budget;
      if (c == '\n') {
        if (!openUploadFromCommand()) return;
      } else if (c != '\r') {
        if (uploadCommand.length() >= 96) { failUpload("COMANDO DEMASIADO LARGO"); return; }
        uploadCommand += c;
      }
    } else if (uploadState == UPLOAD_LENGTH) {
      lengthBytes[lengthIndex++] = (uint8_t)uploadClient.read();
      --budget;
      if (lengthIndex == 4) {
        frameRemaining = ((uint32_t)lengthBytes[0] << 24) |
                         ((uint32_t)lengthBytes[1] << 16) |
                         ((uint32_t)lengthBytes[2] << 8) |
                         (uint32_t)lengthBytes[3];
        lengthIndex = 0;
        if (frameRemaining == 0) {
          if (transferMode == TRANSFER_STORE) finishUpload();
          else if (streamerDirectFinishInput()) uploadState = UPLOAD_DIRECT_WAIT_FINISH;
          else failUpload("FIN DIRECTO EN ESTADO INVALIDO");
          return;
        }
        if (frameRemaining > ESP32_FILE_FRAME_MAX) { failUpload("BLOQUE DEMASIADO GRANDE"); return; }
        if (uploadBytes + frameRemaining > SD_MAX_FILE_BYTES) { failUpload("ARCHIVO DEMASIADO GRANDE"); return; }
        if (transferMode == TRANSFER_DIRECT && frameRemaining > ESP32_DIRECT_LINE_MAX) {
          failUpload("LINEA FUENTE DEMASIADO LARGA");
          return;
        }
        directFrameLength = frameRemaining;
        directLine = "";
        uploadState = UPLOAD_DATA;
      }
    } else if (uploadState == UPLOAD_DATA) {
      if (transferMode == TRANSFER_DIRECT) {
        char c = (char)uploadClient.read();
        directLine += c;
        --frameRemaining;
        --budget;
        ++uploadBytes;
        if (frameRemaining == 0) {
          if (!streamerDirectAcceptLine(directLine, directFrameLength)) {
            failUpload(streamerError().length() ? streamerError() : "LINEA DIRECTA RECHAZADA");
            return;
          }
          uploadState = UPLOAD_DIRECT_WAIT_ACK;
          return;
        }
        continue;
      }
      size_t wanted = min((uint32_t)sizeof(transferBuffer), frameRemaining);
      wanted = min(wanted, (size_t)uploadClient.available());
      wanted = min(wanted, (size_t)budget);
      if (!wanted) break;
      int received = uploadClient.read(transferBuffer, wanted);
      if (received <= 0) break;
      if (!uploadFile || uploadFile.write(transferBuffer, received) != (size_t)received) {
        failUpload("ERROR ESCRIBIENDO SD");
        return;
      }
      uploadBytes += received;
      frameRemaining -= received;
      budget -= received;
      if (frameRemaining == 0) uploadState = UPLOAD_LENGTH;
    }
  }
}

void pollGatewayExternalIp() {
  if (uploadState != UPLOAD_IDLE || millis() - lastGatewayInfoPoll < 4000) return;
  lastGatewayInfoPoll = millis();
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(800);
  http.useHTTP10(true);
  if (!http.begin(client, "http://192.168.4.1/internal-ip")) return;
  int code = http.GET();
  if (code == 200) {
    String value = http.getString();
    value.trim();
    if (value.length() <= 15) gatewayExternalIp = value.length() ? value : "--";
  }
  http.end();
}
} // namespace

void networkInit() {
  lastGatewayEvent.reserve(96);
  uploadCommand.reserve(100);
  uploadName.reserve(64);
  uploadPath.reserve(68);
  uploadFinalPath.reserve(68);
  directLine.reserve(ESP32_DIRECT_LINE_MAX);
  gatewayExternalIp.reserve(20);

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.config(esp32Ip, gatewayIp, subnet);
  WiFi.begin(GATEWAY_AP_SSID, GATEWAY_AP_PASSWORD);
  lastConnectAttempt = millis();
  sys.wifiConnecting = true;
  sys.wifiConnected = false;
  sys.wifiSsid = GATEWAY_AP_SSID;
  sys.wifiIp = esp32Ip.toString();
  sys.apSsid = GATEWAY_AP_SSID;
  sys.apIp = gatewayIp.toString();
  Serial.printf("[GATEWAY] conectando a %s, IP fija %s\n", GATEWAY_AP_SSID, esp32Ip.toString().c_str());
}

void networkLoop() {
  wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    if (!sys.wifiConnected) {
      sys.wifiConnected = true;
      sys.wifiConnecting = false;
      sys.wifiIp = WiFi.localIP().toString();
      lastGatewayEvent = "ESP8266 CONECTADO";
      showToast("ENLACE ESP8266 ACTIVO", C_GREEN, 2200);
      sys.needFullRedraw = true;
      Serial.printf("[GATEWAY] enlace OK, IP %s\n", sys.wifiIp.c_str());
    }
    startServers();
    internalServer.handleClient();
    serviceFileTransfer();
    pollGatewayExternalIp();
  } else {
    if (sys.wifiConnected) {
      sys.wifiConnected = false;
      sys.wifiConnecting = true;
      lastGatewayEvent = "ENLACE ESP8266 PERDIDO";
      showToast("BUSCANDO ESP8266", C_ORANGE, 2200);
      sys.needFullRedraw = true;
    }
    if (millis() - lastConnectAttempt >= GATEWAY_RETRY_MS) {
      lastConnectAttempt = millis();
      WiFi.disconnect(false, false);
      WiFi.config(esp32Ip, gatewayIp, subnet);
      WiFi.begin(GATEWAY_AP_SSID, GATEWAY_AP_PASSWORD);
      Serial.println("[GATEWAY] reintento de enlace");
    }
  }
}

void networkReconnectGateway() {
  if (streamerIsBusy()) {
    showToast("DETENE EL TRABAJO PRIMERO", C_ORANGE, 2200);
    return;
  }
  WiFi.disconnect(false, false);
  delay(40);
  WiFi.config(esp32Ip, gatewayIp, subnet);
  WiFi.begin(GATEWAY_AP_SSID, GATEWAY_AP_PASSWORD);
  lastConnectAttempt = millis();
  sys.wifiConnected = false;
  sys.wifiConnecting = true;
  lastGatewayEvent = "RECONECTANDO ESP8266";
  showToast("RECONECTANDO ESP8266", C_YELLOW, 2200);
  sys.needFullRedraw = true;
}

bool networkGatewayConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool networkConsumeCalibrationRequest() {
  bool value = calibrationRequested;
  calibrationRequested = false;
  return value;
}

String networkUrl() {
  return "http://192.168.4.1";
}

String networkExternalIp() {
  return gatewayExternalIp;
}

String networkLastWebEvent() {
  return lastGatewayEvent;
}
