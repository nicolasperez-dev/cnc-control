#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <FS.h>
#include <LittleFS.h>

#include "config.h"
#include "web_page.h"

void setup();
void loop();

namespace {
ESP8266WebServer web(WEB_PORT);
DNSServer dns;
IPAddress apIp(192, 168, 4, 1);
IPAddress apMask(255, 255, 255, 0);
IPAddress esp32Ip(ESP32_IP_1, ESP32_IP_2, ESP32_IP_3, ESP32_IP_4);

struct StoredWifi {
  uint32_t magic;
  char ssid[33];
  char password[65];
  uint32_t checksum;
};

StoredWifi storedWifi = {};
bool storedWifiValid = false;
bool mdnsReady = false;
unsigned long restartAt = 0;

WiFiClient fileLink;
bool uploadActive = false;
bool uploadOk = false;
String uploadError;
String uploadRemoteReply;
String uploadFileName;
uint32_t uploadBytes = 0;

WiFiClient directLink;
fs::File directUploadFile;
fs::File directRunFile;
bool littleFsReady = false;
bool directUploadOk = false;
bool directEsp32Accepted = false;
bool directUploadOwnsTemp = false;
String directUploadError;
String directFileName;
String directLastMessage = "SIN CORTE DIRECTO";
String directReplyBuffer;
uint32_t directFileBytes = 0;
unsigned long directStateSince = 0;

enum DirectRunState : uint8_t {
  DIRECT_IDLE = 0,
  DIRECT_CONNECTING,
  DIRECT_WAIT_READY,
  DIRECT_SEND_LINE,
  DIRECT_WAIT_NEXT,
  DIRECT_SEND_EOF,
  DIRECT_WAIT_DONE,
  DIRECT_COMPLETED,
  DIRECT_ERROR
};

DirectRunState directState = DIRECT_IDLE;

bool directJobActive() {
  return directState >= DIRECT_CONNECTING && directState <= DIRECT_WAIT_DONE;
}

const char* directStateText() {
  switch (directState) {
    case DIRECT_CONNECTING: return "CONECTANDO";
    case DIRECT_WAIT_READY: return "PREPARANDO ESP32";
    case DIRECT_SEND_LINE:
    case DIRECT_WAIT_NEXT: return "EJECUTANDO";
    case DIRECT_SEND_EOF:
    case DIRECT_WAIT_DONE: return "FINALIZANDO";
    case DIRECT_COMPLETED: return "COMPLETADO";
    case DIRECT_ERROR: return "ERROR";
    default: return "LISTO";
  }
}

uint32_t configChecksum(const StoredWifi& value) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
  const size_t length = sizeof(StoredWifi) - sizeof(value.checksum);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; ++i) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
}

String jsonEscape(const String& input) {
  String output;
  output.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input.charAt(i);
    if (c == '"' || c == '\\') { output += '\\'; output += c; }
    else if (c == '\n') output += "\\n";
    else if ((uint8_t)c >= 32) output += c;
  }
  return output;
}

void sendJson(int code, const String& body) {
  web.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  web.sendHeader("Pragma", "no-cache");
  web.sendHeader("Connection", "close");
  web.send(code, "application/json; charset=utf-8", body);
}

void sendError(int code, const String& message) {
  sendJson(code, "{\"ok\":false,\"error\":\"" + jsonEscape(message) + "\"}");
}

bool loadWifiConfig() {
  EEPROM.begin(EEPROM_BYTES);
  EEPROM.get(0, storedWifi);
  storedWifi.ssid[sizeof(storedWifi.ssid) - 1] = 0;
  storedWifi.password[sizeof(storedWifi.password) - 1] = 0;
  storedWifiValid = storedWifi.magic == WIFI_CONFIG_MAGIC &&
                    storedWifi.ssid[0] != 0 &&
                    storedWifi.checksum == configChecksum(storedWifi);
  if (!storedWifiValid) memset(&storedWifi, 0, sizeof(storedWifi));
  return storedWifiValid;
}

bool saveWifiConfig(const String& ssid, const String& password) {
  StoredWifi next = {};
  next.magic = WIFI_CONFIG_MAGIC;
  ssid.toCharArray(next.ssid, sizeof(next.ssid));
  password.toCharArray(next.password, sizeof(next.password));
  next.checksum = configChecksum(next);
  EEPROM.put(0, next);
  if (!EEPROM.commit()) return false;
  storedWifi = next;
  storedWifiValid = true;
  return true;
}

String esp32Url(const String& path) {
  return "http://" + esp32Ip.toString() + ":" + String(ESP32_HTTP_PORT) + path;
}

int requestEsp32(const String& method, const String& path, const String& body, String& response) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(ESP32_HTTP_TIMEOUT_MS);
  http.useHTTP10(true);
  if (!http.begin(client, esp32Url(path))) {
    response = "No se pudo abrir el enlace con ESP32";
    return -1;
  }
  http.addHeader("Connection", "close");
  int code;
  if (method == "POST") {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    code = http.POST(body);
  } else {
    code = http.GET();
  }
  if (code > 0) response = http.getString();
  else response = http.errorToString(code);
  http.end();
  return code;
}

String encodeArg(const String& value) {
  const char hex[] = "0123456789ABCDEF";
  String output;
  output.reserve(value.length() * 3);
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = (uint8_t)value.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      output += (char)c;
    } else {
      output += '%';
      output += hex[c >> 4];
      output += hex[c & 15];
    }
  }
  return output;
}

void proxyGet(const String& path) {
  String response;
  int code = requestEsp32("GET", path, "", response);
  if (code <= 0) { sendError(503, "Pantalla ESP32 sin respuesta: " + response); return; }
  sendJson(code, response);
}

void proxyPost(const String& path, const String& body) {
  String response;
  int code = requestEsp32("POST", path, body, response);
  if (code <= 0) { sendError(503, "Pantalla ESP32 sin respuesta: " + response); return; }
  sendJson(code, response);
}

String gatewayFields() {
  const bool external = WiFi.status() == WL_CONNECTED;
  String fields;
  fields.reserve(420);
  fields += "\"gatewayVersion\":\"" FW_VERSION "\",";
  fields += "\"externalConnected\":" + String(external ? "true" : "false") + ",";
  fields += "\"externalSsid\":\"" + jsonEscape(external ? WiFi.SSID() : (storedWifiValid ? String(storedWifi.ssid) : String(""))) + "\",";
  fields += "\"externalIp\":\"" + String(external ? WiFi.localIP().toString() : String("")) + "\",";
  fields += "\"apSsid\":\"" CONTROL_AP_SSID "\",";
  fields += "\"apIp\":\"" + WiFi.softAPIP().toString() + "\",";
  fields += "\"directActive\":" + String(directJobActive() ? "true" : "false") + ",";
  fields += "\"directState\":\"" + String(directStateText()) + "\",";
  fields += "\"directMessage\":\"" + jsonEscape(directLastMessage) + "\"";
  return fields;
}

void handleStatus() {
  String response;
  int code = requestEsp32("GET", "/status", "", response);
  String json;
  if (code == 200 && response.startsWith("{") && response.lastIndexOf('}') >= 0) {
    response.remove(response.lastIndexOf('}'));
    json = response + ",\"esp32Online\":true," + gatewayFields() + "}";
  } else {
    json = "{\"ok\":true,\"esp32Online\":false,\"grblConnected\":false,\"sd\":false," + gatewayFields() + "}";
  }
  sendJson(200, json);
}

void handlePing() {
  String response;
  const int code = requestEsp32("GET", "/ping", "", response);
  String json = "{\"ok\":true,\"gateway\":true,\"esp32\":";
  json += code == 200 ? "true" : "false";
  json += ",\"message\":\"";
  json += code == 200 ? "Enlace ESP8266 a ESP32 correcto" : "ESP8266 activo; ESP32 sin respuesta";
  json += "\"}";
  sendJson(200, json);
}

void handleNetworkScan() {
  const int count = WiFi.scanNetworks(false, true);
  if (count < 0) { sendError(500, "No se pudo explorar el WiFi"); return; }
  String json = "{\"ok\":true,\"networks\":[";
  String seen;
  for (int i = 0; i < count; ++i) {
    const String ssid = WiFi.SSID(i);
    if (!ssid.length() || seen.indexOf("\n" + ssid + "\n") >= 0) continue;
    if (json.charAt(json.length() - 1) != '[') json += ',';
    seen += "\n" + ssid + "\n";
    json += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  json += "]}";
  WiFi.scanDelete();
  sendJson(200, json);
}

String sanitizeFileName(String name) {
  name.replace('\\', '/');
  int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);
  name.trim();
  String clean;
  clean.reserve(64);
  for (size_t i = 0; i < name.length() && clean.length() < 60; ++i) {
    const char c = name.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_') clean += c;
    else if (c == ' ') clean += '_';
  }
  return clean;
}

bool allowedFileName(const String& name) {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".nc") || lower.endsWith(".gcode") ||
         lower.endsWith(".tap") || lower.endsWith(".cnc");
}

void closeUploadLink() {
  if (fileLink) fileLink.stop();
  uploadActive = false;
}

void failUpload(const String& message) {
  uploadOk = false;
  uploadError = message;
  closeUploadLink();
}

bool writeAll(WiFiClient& client, const uint8_t* data, size_t length) {
  size_t sent = 0;
  unsigned long lastProgress = millis();
  while (sent < length) {
    if (!client.connected()) return false;
    size_t amount = client.write(data + sent, length - sent);
    if (amount) {
      sent += amount;
      lastProgress = millis();
    } else {
      if (millis() - lastProgress > ESP32_FILE_WRITE_MS) return false;
      delay(1);
      yield();
    }
  }
  return true;
}

bool sendFrame(WiFiClient& client, const uint8_t* data, size_t length) {
  uint8_t header[4] = {
    (uint8_t)((length >> 24) & 0xFF),
    (uint8_t)((length >> 16) & 0xFF),
    (uint8_t)((length >> 8) & 0xFF),
    (uint8_t)(length & 0xFF)
  };
  return writeAll(client, header, sizeof(header)) && (!length || writeAll(client, data, length));
}

bool readReplyLine(WiFiClient& client, String& reply, unsigned long timeoutMs = ESP32_FILE_REPLY_MS) {
  reply = "";
  unsigned long started = millis();
  while (millis() - started < timeoutMs) {
    while (client.available()) {
      const char c = (char)client.read();
      if (c == '\n') { reply.trim(); return reply.length() > 0; }
      if (c != '\r' && reply.length() < 160) reply += c;
    }
    if (!client.connected() && !client.available()) break;
    delay(1);
    yield();
  }
  reply.trim();
  return reply.length() > 0;
}

String uploadLinkError(const String& fallback) {
  String reply;
  if (fileLink.available() && readReplyLine(fileLink, reply, 150UL)) {
    if (reply.startsWith("ERR|")) return reply.substring(4);
    if (reply.length()) return reply;
  }
  return fallback;
}

void handleUploadData() {
  HTTPUpload& upload = web.upload();
  if (upload.status == UPLOAD_FILE_START) {
    closeUploadLink();
    uploadOk = false;
    uploadError = "";
    uploadRemoteReply = "";
    uploadBytes = 0;
    uploadFileName = sanitizeFileName(upload.filename);
    if (directJobActive()) {
      uploadError = "Hay un corte directo en curso";
      return;
    }
    if (!allowedFileName(uploadFileName)) {
      uploadError = "Extension no admitida; usa .nc, .gcode, .tap o .cnc";
      return;
    }
    if (!fileLink.connect(esp32Ip, ESP32_FILE_PORT)) {
      uploadError = "No se pudo conectar con la pantalla ESP32";
      return;
    }
    fileLink.setNoDelay(true);
    const String command = "PUT " + uploadFileName + "\n";
    if (!writeAll(fileLink, reinterpret_cast<const uint8_t*>(command.c_str()), command.length())) {
      failUpload("No se pudo iniciar la transferencia");
      return;
    }
    if (!readReplyLine(fileLink, uploadRemoteReply)) {
      failUpload("La pantalla ESP32 no confirmo que la microSD estuviera lista");
      return;
    }
    if (!uploadRemoteReply.startsWith("READY|PUT|")) {
      const String reason = uploadRemoteReply.startsWith("ERR|")
                              ? uploadRemoteReply.substring(4)
                              : uploadRemoteReply;
      failUpload(reason.length() ? reason : "La pantalla rechazo la transferencia");
      return;
    }
    uploadActive = true;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!uploadActive || uploadError.length()) return;
    if (uploadBytes + upload.currentSize > MAX_GCODE_BYTES) {
      failUpload("Archivo demasiado grande; maximo 32 MB");
      return;
    }
    size_t offset = 0;
    while (offset < upload.currentSize) {
      const size_t amount = min((size_t)4096, upload.currentSize - offset);
      if (!sendFrame(fileLink, upload.buf + offset, amount)) {
        failUpload(uploadLinkError("Se corto el enlace durante la transferencia"));
        return;
      }
      if (fileLink.available()) {
        String earlyReply;
        if (readReplyLine(fileLink, earlyReply, 150UL) && earlyReply.startsWith("ERR|")) {
          failUpload(earlyReply.substring(4));
          return;
        }
      }
      offset += amount;
      uploadBytes += amount;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!uploadActive || uploadError.length()) { closeUploadLink(); return; }
    if (!sendFrame(fileLink, nullptr, 0)) {
      failUpload(uploadLinkError("No se pudo cerrar la transferencia"));
      return;
    }
    if (!readReplyLine(fileLink, uploadRemoteReply)) {
      failUpload("La pantalla no confirmo el guardado en SD");
      return;
    }
    uploadOk = uploadRemoteReply.startsWith("OK|");
    if (!uploadOk) uploadError = uploadRemoteReply.startsWith("ERR|") ? uploadRemoteReply.substring(4) : uploadRemoteReply;
    closeUploadLink();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    failUpload("Carga cancelada por el navegador");
  }
}

void handleUploadResult() {
  if (uploadOk) {
    sendJson(200, "{\"ok\":true,\"message\":\"Archivo " + jsonEscape(uploadFileName) +
                  " guardado en la microSD de la ESP32\",\"bytes\":" + String(uploadBytes) + "}");
  } else {
    sendError(500, uploadError.length() ? uploadError : "La carga no termino correctamente");
  }
}

void closeDirectResources(bool removeTemporary) {
  if (directUploadFile) directUploadFile.close();
  if (directRunFile) directRunFile.close();
  if (directLink) directLink.stop();
  if (removeTemporary && littleFsReady) LittleFS.remove(DIRECT_TEMP_PATH);
}

void setDirectState(DirectRunState state) {
  directState = state;
  directStateSince = millis();
}

void failDirectRun(const String& message) {
  const bool mustStopEsp32 = directEsp32Accepted;
  closeDirectResources(true);
  directEsp32Accepted = false;
  directLastMessage = message;
  setDirectState(DIRECT_ERROR);
  Serial.printf("[DIRECT] ERROR: %s\n", message.c_str());
  if (mustStopEsp32) {
    String ignored;
    requestEsp32("POST", "/action", "name=stop", ignored);
  }
}

void completeDirectRun(const String& reply) {
  closeDirectResources(true);
  directEsp32Accepted = false;
  directLastMessage = "Corte directo completado: " + directFileName;
  setDirectState(DIRECT_COMPLETED);
  Serial.printf("[DIRECT] OK: %s | %s\n", directFileName.c_str(), reply.c_str());
}

bool takeDirectReply(String& reply) {
  while (directLink.available()) {
    const char c = (char)directLink.read();
    if (c == '\n') {
      directReplyBuffer.trim();
      reply = directReplyBuffer;
      directReplyBuffer = "";
      return reply.length() > 0;
    }
    if (c != '\r' && directReplyBuffer.length() < 180) directReplyBuffer += c;
  }
  return false;
}

bool readDirectSourceLine(String& line) {
  line = "";
  while (directRunFile && directRunFile.available()) {
    const int value = directRunFile.read();
    if (value < 0) break;
    if (line.length() >= DIRECT_SOURCE_LINE_MAX) {
      failDirectRun("Una linea del archivo supera 512 caracteres");
      return false;
    }
    line += (char)value;
    if (value == '\n') return true;
  }
  return line.length() > 0;
}

bool startDirectRun(const String& fileName, uint32_t fileBytes) {
  if (!littleFsReady || !fileBytes || directJobActive() || uploadActive) return false;
  directFileName = fileName;
  directFileBytes = fileBytes;
  directReplyBuffer = "";
  directEsp32Accepted = false;
  directLastMessage = "Preparando corte directo: " + directFileName;
  setDirectState(DIRECT_CONNECTING);
  return true;
}

void serviceDirectRun() {
  if (!directJobActive()) return;

  if (directState == DIRECT_CONNECTING) {
    if (!directLink.connect(esp32Ip, ESP32_FILE_PORT)) {
      failDirectRun("No se pudo conectar con la pantalla ESP32");
      return;
    }
    directLink.setNoDelay(true);
    const String command = "RUN " + directFileName + " " + String(directFileBytes) + "\n";
    if (!writeAll(directLink, reinterpret_cast<const uint8_t*>(command.c_str()), command.length())) {
      failDirectRun("No se pudo iniciar el enlace de corte directo");
      return;
    }
    setDirectState(DIRECT_WAIT_READY);
    return;
  }

  if (directState == DIRECT_SEND_LINE) {
    String line;
    if (readDirectSourceLine(line)) {
      if (!sendFrame(directLink, reinterpret_cast<const uint8_t*>(line.c_str()), line.length())) {
        failDirectRun("Se corto el enlace mientras se enviaba G-code");
        return;
      }
      setDirectState(DIRECT_WAIT_NEXT);
    } else if (directState != DIRECT_ERROR) {
      setDirectState(DIRECT_SEND_EOF);
    }
    return;
  }

  if (directState == DIRECT_SEND_EOF) {
    if (!sendFrame(directLink, nullptr, 0)) {
      failDirectRun("No se pudo cerrar el corte directo");
      return;
    }
    setDirectState(DIRECT_WAIT_DONE);
    return;
  }

  String reply;
  if (takeDirectReply(reply)) {
    if (reply.startsWith("ERR|")) {
      failDirectRun(reply.substring(4));
      return;
    }
    if (directState == DIRECT_WAIT_READY) {
      if (!reply.startsWith("READY|RUN|")) {
        failDirectRun("Respuesta inesperada de ESP32: " + reply);
        return;
      }
      directEsp32Accepted = true;
      directRunFile = LittleFS.open(DIRECT_TEMP_PATH, "r");
      if (!directRunFile) {
        failDirectRun("No se pudo reabrir el G-code temporal");
        return;
      }
      directLastMessage = "Corte directo en ejecucion: " + directFileName;
      setDirectState(DIRECT_SEND_LINE);
      return;
    }
    if (directState == DIRECT_WAIT_NEXT) {
      if (!reply.startsWith("NEXT|")) {
        failDirectRun("Confirmacion de linea invalida: " + reply);
        return;
      }
      setDirectState(DIRECT_SEND_LINE);
      return;
    }
    if (directState == DIRECT_WAIT_DONE) {
      if (!reply.startsWith("OK|DIRECT|")) {
        failDirectRun("Confirmacion final invalida: " + reply);
        return;
      }
      completeDirectRun(reply);
      return;
    }
  }

  if (!directLink.connected() && !directLink.available()) {
    failDirectRun("Se perdio el enlace con la pantalla ESP32");
    return;
  }

  const unsigned long timeout = directState == DIRECT_WAIT_READY
                                  ? DIRECT_HANDSHAKE_MS
                                  : DIRECT_REPLY_TIMEOUT_MS;
  if (millis() - directStateSince > timeout) {
    failDirectRun(directState == DIRECT_WAIT_READY
                    ? "La pantalla no preparo el corte directo a tiempo"
                    : "La maquina no confirmo el avance del corte directo");
  }
}

void failDirectUpload(const String& message) {
  directUploadOk = false;
  directUploadError = message;
  if (directUploadFile) directUploadFile.close();
  if (littleFsReady && directUploadOwnsTemp) LittleFS.remove(DIRECT_TEMP_PATH);
  directUploadOwnsTemp = false;
}

void handleDirectUploadData() {
  HTTPUpload& upload = web.upload();
  if (upload.status == UPLOAD_FILE_START) {
    directUploadOk = false;
    directUploadError = "";
    directFileBytes = 0;
    directUploadOwnsTemp = false;
    directFileName = sanitizeFileName(upload.filename);
    if (!littleFsReady) { directUploadError = "Memoria temporal LittleFS no disponible"; return; }
    // No borres DIRECT_TEMP_PATH si ya lo esta leyendo un trabajo activo.
    if (directJobActive()) { directUploadError = "Ya hay un corte directo en curso"; return; }
    if (uploadActive) { directUploadError = "Hay una transferencia a microSD en curso"; return; }
    if (!allowedFileName(directFileName)) {
      failDirectUpload("Extension no admitida; usa .nc, .gcode, .tap o .cnc");
      return;
    }
    LittleFS.remove(DIRECT_TEMP_PATH);
    directUploadFile = LittleFS.open(DIRECT_TEMP_PATH, "w");
    directUploadOwnsTemp = (bool)directUploadFile;
    if (!directUploadFile) failDirectUpload("No se pudo crear el temporal en ESP8266");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (directUploadError.length() || !directUploadFile) return;
    if (directFileBytes + upload.currentSize > DIRECT_MAX_GCODE_BYTES) {
      failDirectUpload("Corte directo admite hasta 768 KB");
      return;
    }
    if (directUploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
      failDirectUpload("No hay espacio para el temporal en ESP8266");
      return;
    }
    directFileBytes += upload.currentSize;
  } else if (upload.status == UPLOAD_FILE_END) {
    if (directUploadFile) directUploadFile.close();
    if (directUploadError.length()) return;
    if (!directFileBytes) { failDirectUpload("El archivo esta vacio"); return; }
    directUploadOk = true;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    failDirectUpload("Carga directa cancelada por el navegador");
  }
}

void handleDirectUploadResult() {
  if (!directUploadOk) {
    sendError(500, directUploadError.length() ? directUploadError : "No se recibio el archivo directo");
    return;
  }
  if (!startDirectRun(directFileName, directFileBytes)) {
    failDirectUpload("No se pudo iniciar el corte directo");
    sendError(409, directUploadError);
    return;
  }
  directUploadOwnsTemp = false;
  // Confirma el READY de la ESP32 antes de decirle al navegador que el corte
  // comenzo. Asi errores como GRBL desconectado u ocupado llegan de inmediato.
  serviceDirectRun();
  const unsigned long started = millis();
  while (directState == DIRECT_WAIT_READY && millis() - started <= DIRECT_HANDSHAKE_MS + 100UL) {
    serviceDirectRun();
    delay(1);
    yield();
  }
  if (directState == DIRECT_ERROR) {
    sendError(409, directLastMessage);
    return;
  }
  if (directState != DIRECT_SEND_LINE) {
    failDirectRun("La pantalla ESP32 no habilito el corte directo");
    sendError(409, directLastMessage);
    return;
  }
  sendJson(200, "{\"ok\":true,\"message\":\"Archivo recibido; el corte directo comenzo sin usar la microSD\",\"bytes\":" +
                String(directFileBytes) + "}");
}

void handleWifiSave() {
  String ssid = web.arg("ssid");
  String password = web.arg("pass");
  ssid.trim();
  if (!ssid.length() || ssid.length() > 32) { sendError(400, "SSID invalido"); return; }
  if (password.length() > 63 || (password.length() > 0 && password.length() < 8)) {
    sendError(400, "La clave debe estar vacia o tener entre 8 y 63 caracteres");
    return;
  }
  if (!saveWifiConfig(ssid, password)) { sendError(500, "No se pudieron guardar las credenciales"); return; }
  sendJson(200, "{\"ok\":true,\"message\":\"Credenciales guardadas; reiniciando\"}");
  restartAt = millis() + RESTART_AFTER_SAVE_MS;
}

void redirectToHome() {
  web.sendHeader("Location", "http://192.168.4.1/", true);
  web.sendHeader("Cache-Control", "no-store");
  web.send(302, "text/plain", "CNCBAIRES");
}

void configureWeb() {
  web.on("/", HTTP_GET, []() {
    web.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    web.sendHeader("Pragma", "no-cache");
    web.send_P(200, "text/html; charset=utf-8", WEB_PAGE);
  });
  web.on("/api/status", HTTP_GET, handleStatus);
  web.on("/internal-ip", HTTP_GET, []() {
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "text/plain; charset=utf-8",
             WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("--"));
  });
  web.on("/api/files", HTTP_GET, []() { proxyGet("/files"); });
  web.on("/api/networks", HTTP_GET, handleNetworkScan);
  web.on("/api/ping", HTTP_GET, handlePing);
  web.on("/api/action", HTTP_POST, []() {
    proxyPost("/action", "name=" + encodeArg(web.arg("name")));
  });
  web.on("/api/jog", HTTP_POST, []() {
    proxyPost("/jog", "dx=" + encodeArg(web.arg("dx")) +
                       "&dy=" + encodeArg(web.arg("dy")) +
                       "&dz=" + encodeArg(web.arg("dz")) +
                       "&feed=" + encodeArg(web.arg("feed")));
  });
  web.on("/api/command", HTTP_POST, []() {
    proxyPost("/command", "line=" + encodeArg(web.arg("line")));
  });
  web.on("/api/select", HTTP_POST, []() {
    proxyPost("/select", "index=" + encodeArg(web.arg("index")));
  });
  web.on("/api/delete", HTTP_POST, []() {
    proxyPost("/delete", "index=" + encodeArg(web.arg("index")));
  });
  web.on("/api/wifi", HTTP_POST, handleWifiSave);
  web.on("/upload-gcode", HTTP_POST, handleUploadResult, handleUploadData);
  web.on("/run-gcode", HTTP_POST, handleDirectUploadResult, handleDirectUploadData);

  web.on("/generate_204", HTTP_ANY, redirectToHome);
  web.on("/gen_204", HTTP_ANY, redirectToHome);
  web.on("/hotspot-detect.html", HTTP_ANY, redirectToHome);
  web.on("/connecttest.txt", HTTP_ANY, redirectToHome);
  web.on("/ncsi.txt", HTTP_ANY, redirectToHome);
  web.onNotFound([]() {
    if (web.method() == HTTP_GET) redirectToHome();
    else sendError(404, "Ruta inexistente");
  });
}

void connectExternalWifi() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(DEVICE_HOSTNAME);
  if (!storedWifiValid) return;
  Serial.printf("[WIFI] conectando a %s", storedWifi.ssid);
  WiFi.begin(storedWifi.ssid, storedWifi.password);
  const unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < EXTERNAL_WIFI_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI] internet/red externa OK: %s\n", WiFi.localIP().toString().c_str());
  } else {
    WiFi.disconnect(false);
    Serial.println("[WIFI] red externa no disponible; continua la red privada");
  }
}

void startControlAccessPoint() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIp, apIp, apMask);
  const int channel = WiFi.status() == WL_CONNECTED ? WiFi.channel() : CONTROL_AP_CHANNEL;
  bool started = WiFi.softAP(CONTROL_AP_SSID, CONTROL_AP_PASSWORD, channel, false, CONTROL_AP_MAX_CLIENTS);
  Serial.printf("[AP] %s | IP %s | canal %d | %s\n", CONTROL_AP_SSID,
                WiFi.softAPIP().toString().c_str(), channel, started ? "OK" : "ERROR");
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.printf("%s v%s\n", FW_NAME, FW_VERSION);

  littleFsReady = LittleFS.begin();
  if (littleFsReady) {
    LittleFS.remove(DIRECT_TEMP_PATH);
    Serial.println("[FS] LittleFS listo para corte directo");
  } else {
    Serial.println("[FS] ERROR montando LittleFS; corte directo no disponible");
  }

  loadWifiConfig();
  connectExternalWifi();
  startControlAccessPoint();

  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", apIp);
  if (MDNS.begin(DEVICE_HOSTNAME)) {
    MDNS.addService("http", "tcp", WEB_PORT);
    mdnsReady = true;
    Serial.println("[MDNS] http://cncbaires.local");
  }

  configureWeb();
  web.begin();
  Serial.println("[WEB] http://192.168.4.1");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WEB] red externa: http://%s\n", WiFi.localIP().toString().c_str());
  }
}

void loop() {
  dns.processNextRequest();
  web.handleClient();
  serviceDirectRun();
  if (mdnsReady) MDNS.update();
  if (restartAt && (long)(millis() - restartAt) >= 0) {
    delay(50);
    ESP.restart();
  }
  yield();
}
