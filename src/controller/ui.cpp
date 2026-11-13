#include "ui.h"
#include "config.h"
#include "state.h"
#include "grbl_comm.h"
#include "gcode_streamer.h"
#include "sd_manager.h"
#include "network_manager.h"
#include "touch_driver.h"
#include "TFT_eSPI.h"

extern TFT_eSPI tft;

namespace {
constexpr int HEADER_H = 20;
constexpr int TAB_Y = 220;
constexpr int TAB_H = 20;
unsigned long lastDynamicDraw = 0;
unsigned long deleteArmedUntil = 0;

bool hit(int px, int py, int x, int y, int w, int h) {
  return px >= x && px < x + w && py >= y && py < y + h;
}

void panel(int x, int y, int w, int h) {
  tft.fillRoundRect(x, y, w, h, 4, C_BLACK);
  tft.drawRoundRect(x, y, w, h, 4, C_BORDER);
}

void centeredText(const String& text, int x, int y, int w, int h, uint16_t fg, uint16_t bg, uint8_t size = 1) {
  tft.setTextSize(size);
  tft.setTextColor(fg, bg);
  int tw = (int)text.length() * 6 * size;
  int th = 8 * size;
  tft.setCursor(x + max(2, (w - tw) / 2), y + max(1, (h - th) / 2));
  tft.print(text);
}

void button(int x, int y, int w, int h, const String& label, uint16_t fill = C_YELLOW, uint16_t fg = C_BLACK, uint8_t size = 1) {
  tft.fillRoundRect(x, y, w, h, 5, fill);
  tft.drawRoundRect(x, y, w, h, 5, C_BORDER);
  centeredText(label, x, y, w, h, fg, fill, size);
}

void drawHeader(const char* title) {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, C_BLACK);
  tft.drawFastHLine(0, HEADER_H - 1, SCREEN_W, C_YELLOW);
  tft.setTextColor(C_YELLOW, C_BLACK);
  tft.setTextSize(1);
  tft.setCursor(5, 6);
  tft.print(title);
}

void drawHeaderDynamic() {
  tft.fillRect(150, 2, 169, 16, C_BLACK);
  tft.setTextSize(1);
  int x = 154;
  tft.setTextColor(sys.grblConnected ? C_GREEN : C_RED, C_BLACK);
  tft.setCursor(x, 6); tft.print(sys.grblConnected ? "GRBL" : "GRBL--");
  x += 45;
  tft.setTextColor(sys.sdMounted ? C_GREEN : C_RED, C_BLACK);
  tft.setCursor(x, 6); tft.print("SD");
  x += 25;
  tft.setTextColor(sys.wifiConnected ? C_GREEN : C_ORANGE, C_BLACK);
  tft.setCursor(x, 6); tft.print(sys.wifiConnected ? "8266" : "8266--");
}

void drawTabs() {
  static const char* labels[4] = {"INICIO", "MANUAL", "ARCHIVOS", "ENLACE"};
  for (int i = 0; i < 4; ++i) {
    int x = i * 80;
    uint16_t bg = sys.page == (UiPage)i ? C_YELLOW : C_PANEL_2;
    uint16_t fg = sys.page == (UiPage)i ? C_BLACK : C_WHITE;
    tft.fillRect(x, TAB_Y, 79, TAB_H, bg);
    tft.drawRect(x, TAB_Y, 79, TAB_H, C_BORDER);
    centeredText(labels[i], x, TAB_Y, 79, TAB_H, fg, bg, 1);
  }
}

String shortName(const String& name, int maxChars) {
  if ((int)name.length() <= maxChars) return name;
  return name.substring(0, maxChars - 2) + "..";
}

String formatBytes(uint32_t bytes) {
  if (bytes >= 1024UL * 1024UL) return String(bytes / 1048576.0f, 1) + " MB";
  if (bytes >= 1024UL) return String(bytes / 1024.0f, 1) + " KB";
  return String(bytes) + " B";
}

uint16_t jobColor() {
  switch (streamerState()) {
    case STREAM_RUNNING: return C_GREEN;
    case STREAM_PAUSED: return C_ORANGE;
    case STREAM_ERROR: return C_RED;
    case STREAM_ABORTED: return C_RED;
    case STREAM_COMPLETED: return C_GREEN;
    default: return C_YELLOW;
  }
}

void drawProgressBar(int x, int y, int w, int h, uint8_t percent) {
  tft.fillRect(x, y, w, h, C_DARK_GRAY);
  int filled = (w - 2) * percent / 100;
  if (filled > 0) tft.fillRect(x + 1, y + 1, filled, h - 2, C_YELLOW);
  tft.drawRect(x, y, w, h, C_WHITE);
  centeredText(String(percent) + "%", x, y, w, h, percent > 45 ? C_BLACK : C_WHITE, percent > 45 ? C_YELLOW : C_DARK_GRAY, 1);
}

void drawDashboardStatic() {
  drawHeader("CNCBAIRES");
  panel(2, 22, 122, 110);
  panel(127, 22, 191, 110);

  tft.setTextSize(1);
  tft.setTextColor(C_YELLOW, C_BLACK);
  tft.setCursor(26, 28); tft.print("POSICION ACTUAL (mm)");
  tft.setCursor(181, 28); tft.print("ESTADO");

  tft.setTextSize(2);
  tft.setTextColor(C_YELLOW, C_BLACK);
  tft.setCursor(9, 50); tft.print("X");
  tft.setCursor(9, 78); tft.print("Y");
  tft.setCursor(9, 106); tft.print("Z");

  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_BLACK);
  tft.setCursor(134, 47); tft.print("GRBL:");
  tft.setCursor(134, 64); tft.print("ARCHIVO:");
  tft.setCursor(134, 81); tft.print("LINEA:");
  tft.setCursor(134, 98); tft.print("PROGRESO:");

  button(4, 137, 74, 40, "START", C_GREEN, C_BLACK, 2);
  button(84, 137, 74, 40, "PAUSA", C_ORANGE, C_BLACK, 1);
  button(164, 137, 74, 40, "STOP", C_RED, C_WHITE, 2);
  button(244, 137, 72, 40, "RESET", C_RED, C_WHITE, 1);

  button(4, 181, 74, 35, "ZERO XY");
  button(84, 181, 74, 35, "ZERO Z");
  button(164, 181, 74, 35, "MANUAL");
  button(244, 181, 72, 35, "UNLOCK");
  drawTabs();
}

void drawDashboardDynamic() {
  tft.setTextSize(2);
  tft.setTextColor(C_WHITE, C_BLACK);
  tft.fillRect(29, 45, 91, 80, C_BLACK);
  tft.setCursor(29, 50); tft.printf("%8.3f", sys.workX);
  tft.setCursor(29, 78); tft.printf("%8.3f", sys.workY);
  tft.setCursor(29, 106); tft.printf("%8.3f", sys.workZ);

  tft.fillRect(181, 42, 132, 55, C_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(sys.grblConnected ? C_GREEN : C_RED, C_BLACK);
  tft.setCursor(181, 47); tft.print(shortName(sys.grblState, 20));
  String file = streamerFileName().length() ? streamerFileName() : sdSelectedName();
  tft.setTextColor(C_YELLOW, C_BLACK);
  tft.setCursor(181, 64); tft.print(shortName(file.length() ? file : "--", 21));
  tft.setTextColor(C_WHITE, C_BLACK);
  tft.setCursor(181, 81); tft.print(streamerCurrentLine());
  drawProgressBar(181, 99, 130, 17, streamerProgressPercent());

  tft.fillRect(130, 117, 182, 12, C_BLACK);
  tft.setTextColor(jobColor(), C_BLACK);
  tft.setCursor(134, 119);
  tft.print(streamerStateText());
}

void drawManualStatic() {
  drawHeader("CONTROLES MANUALES");
  panel(2, 22, 226, 34);
  panel(232, 22, 86, 194);
  tft.setTextSize(1);
  tft.setTextColor(C_YELLOW, C_BLACK);
  tft.setCursor(244, 29); tft.print("PASO mm");

  // Pad XY de 8 direcciones.
  const int xs[3] = {4, 62, 120};
  const int ys[3] = {61, 104, 147};
  const char* labels[3][3] = {{"X-Y+", "Y+", "X+Y+"}, {"X-", "HOLD", "X+"}, {"X-Y-", "Y-", "X+Y-"}};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      uint16_t fill = (r == 1 && c == 1) ? C_ORANGE : C_YELLOW;
      button(xs[c], ys[r], 54, 38, labels[r][c], fill, C_BLACK, 1);
    }
  }
  button(180, 61, 46, 60, "Z+", C_YELLOW, C_BLACK, 2);
  button(180, 127, 46, 58, "Z-", C_YELLOW, C_BLACK, 2);

  String stepText = currentStep() < 1.0f ? String(currentStep(), 1) : String((int)currentStep());
  button(237, 44, 76, 36, stepText, C_WHITE, C_BLACK, 2);
  tft.setTextSize(1);
  tft.setTextColor(C_YELLOW, C_BLACK);
  tft.setCursor(239, 91); tft.print("VEL mm/min");
  button(237, 106, 76, 36, String(currentJogFeed()), C_CYAN, C_BLACK, 1);
  tft.setTextColor(C_GRAY, C_BLACK);
  tft.setCursor(239, 154); tft.print("TOCAR PARA");
  tft.setCursor(246, 166); tft.print("CAMBIAR");

  button(4, 190, 70, 26, "ZERO XY");
  button(78, 190, 70, 26, "ZERO Z");
  button(152, 190, 74, 26, "UNLOCK");
  drawTabs();
}

void drawManualDynamic() {
  tft.fillRect(8, 28, 214, 22, C_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_BLACK);
  tft.setCursor(8, 31); tft.printf("X:%7.2f", sys.workX);
  tft.setCursor(79, 31); tft.printf("Y:%7.2f", sys.workY);
  tft.setCursor(150, 31); tft.printf("Z:%7.2f", sys.workZ);
  tft.setTextColor(sys.grblConnected ? C_GREEN : C_RED, C_BLACK);
  tft.setCursor(8, 43); tft.print(shortName(sys.grblState, 14));
  tft.setTextColor(C_CYAN, C_BLACK);
  tft.setCursor(104, 43); tft.printf("JOG:%u mm/min", currentJogFeed());
}

void drawFilesStatic() {
  drawHeader("ARCHIVOS G-CODE / MICROSD");
  panel(2, 22, 205, 166);
  panel(211, 22, 107, 166);
  tft.setTextSize(1);
  tft.setTextColor(C_YELLOW, C_BLACK);
  tft.setCursor(8, 28); tft.print(sys.sdMounted ? "ARCHIVOS" : "SD NO DISPONIBLE");

  for (int row = 0; row < 5; ++row) {
    int index = sys.fileListOffset + row;
    int y = 42 + row * 27;
    const SdFileInfo* file = sdFileAt(index);
    uint16_t bg = index == sdSelectedIndex() ? C_YELLOW : C_BLACK;
    uint16_t fg = index == sdSelectedIndex() ? C_BLACK : C_WHITE;
    tft.fillRect(6, y, 197, 24, bg);
    tft.drawRect(6, y, 197, 24, C_DARK_GRAY);
    if (file) {
      tft.setTextColor(fg, bg);
      tft.setCursor(10, y + 4); tft.print(shortName(file->name, 23));
      tft.setCursor(152, y + 14); tft.print(formatBytes(file->size));
    }
  }

  button(216, 28, 97, 27, "ARRIBA");
  button(216, 60, 97, 27, "ABAJO");
  button(216, 92, 97, 27, "REFRESCAR");
  button(216, 124, 97, 27, "INICIAR", C_GREEN, C_BLACK);
  button(216, 156, 97, 27, "ELIMINAR", C_RED, C_WHITE);

  panel(2, 190, 316, 26);
  drawTabs();
}

void drawFilesDynamic() {
  tft.fillRect(7, 194, 306, 18, C_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(C_YELLOW, C_BLACK);
  String selected = sdSelectedName().length() ? sdSelectedName() : "SIN ARCHIVO";
  tft.setCursor(8, 194); tft.print(shortName(selected, 27));
  tft.setTextColor(jobColor(), C_BLACK);
  tft.setCursor(8, 204); tft.printf("%s  L:%lu  %u%%", streamerStateText(), (unsigned long)streamerCurrentLine(), streamerProgressPercent());
}

void drawNetworkStatic() {
  drawHeader("ENLACE PRIVADO ESP8266");
  panel(2, 22, 316, 136);
  tft.setTextSize(1);
  tft.setTextColor(C_YELLOW, C_BLACK);
  tft.setCursor(8, 28); tft.print("MODULO DE RED EXTERNO");
  tft.setTextColor(C_WHITE, C_BLACK);
  tft.setCursor(8, 46); tft.print("Estado:");
  tft.setCursor(8, 63); tft.print("Red interna:");
  tft.setCursor(8, 80); tft.print("IP ESP32:");
  tft.setCursor(8, 97); tft.print("IP externa:");
  tft.setCursor(8, 114); tft.print("Pagina web:");
  tft.setCursor(8, 131); tft.print("API interna:");
  tft.setCursor(8, 148); tft.print("Ultimo evento:");

  button(4, 163, 312, 24, "RECONECTAR ESP8266");
  button(4, 191, 153, 25, "CALIBRAR TOUCH");
  button(163, 191, 153, 25, "REINICIAR", C_RED, C_WHITE);
  drawTabs();
}

void drawNetworkDynamic() {
  tft.fillRect(100, 42, 210, 112, C_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(sys.wifiConnected ? C_GREEN : C_ORANGE, C_BLACK);
  tft.setCursor(100, 46); tft.print(sys.wifiConnected ? "CONECTADO" : "BUSCANDO ESP8266...");
  tft.setTextColor(C_YELLOW, C_BLACK);
  tft.setCursor(100, 63); tft.print(shortName(sys.wifiSsid, 30));
  tft.setTextColor(C_WHITE, C_BLACK);
  tft.setCursor(100, 80); tft.print(sys.wifiIp.length() ? sys.wifiIp : "192.168.4.2");
  tft.setTextColor(C_CYAN, C_BLACK);
  tft.setCursor(100, 97); tft.print(networkExternalIp());
  tft.setCursor(100, 114); tft.print(networkExternalIp() != "--" ? "http://" + networkExternalIp() : "http://192.168.4.1");
  tft.setTextColor(C_WHITE, C_BLACK);
  tft.setCursor(100, 131); tft.printf("192.168.4.2:%u", ESP32_INTERNAL_HTTP_PORT);
  tft.setTextColor(C_GRAY, C_BLACK);
  tft.setCursor(100, 148); tft.print(shortName(networkLastWebEvent(), 34));
}

void drawToast() {
  if (!sys.toast.length()) return;
  int width = min(306, max(150, (int)sys.toast.length() * 6 + 18));
  int x = (SCREEN_W - width) / 2;
  int y = 94;
  tft.fillRoundRect(x, y, width, 38, 6, sys.toastColor);
  tft.drawRoundRect(x, y, width, 38, 6, C_WHITE);
  centeredText(shortName(sys.toast, 46), x, y, width, 38, sys.toastColor == C_RED ? C_WHITE : C_BLACK, sys.toastColor, 1);
}

void drawFullPage() {
  tft.fillScreen(C_BLACK);
  switch (sys.page) {
    case PAGE_DASHBOARD: drawDashboardStatic(); drawDashboardDynamic(); break;
    case PAGE_MANUAL: drawManualStatic(); drawManualDynamic(); break;
    case PAGE_FILES: drawFilesStatic(); drawFilesDynamic(); break;
    case PAGE_NETWORK: drawNetworkStatic(); drawNetworkDynamic(); break;
  }
  drawHeaderDynamic();
  drawToast();
  sys.needFullRedraw = false;
}

void drawDynamic() {
  drawHeaderDynamic();
  switch (sys.page) {
    case PAGE_DASHBOARD: drawDashboardDynamic(); break;
    case PAGE_MANUAL: drawManualDynamic(); break;
    case PAGE_FILES: drawFilesDynamic(); break;
    case PAGE_NETWORK: drawNetworkDynamic(); break;
  }
  drawToast();
}

void handleTabs(int x, int y) {
  if (y < TAB_Y) return;
  int page = constrain(x / 80, 0, 3);
  sys.page = (UiPage)page;
  sys.needFullRedraw = true;
}

void manualJogFromCell(int col, int row) {
  float step = currentStep();
  if (row == 0 && col == 0) grblJogXY(-step, step);
  else if (row == 0 && col == 1) grblJogXY(0, step);
  else if (row == 0 && col == 2) grblJogXY(step, step);
  else if (row == 1 && col == 0) grblJogXY(-step, 0);
  else if (row == 1 && col == 1) {
    if (streamerIsBusy()) streamerPause(); else grblRealtimeHold();
  }
  else if (row == 1 && col == 2) grblJogXY(step, 0);
  else if (row == 2 && col == 0) grblJogXY(-step, -step);
  else if (row == 2 && col == 1) grblJogXY(0, -step);
  else if (row == 2 && col == 2) grblJogXY(step, -step);
}

void handleDashboardTap(int x, int y) {
  if (hit(x, y, 4, 137, 74, 40)) streamerStartSelected();
  else if (hit(x, y, 84, 137, 74, 40)) streamerPause();
  else if (hit(x, y, 164, 137, 74, 40)) streamerStop();
  else if (hit(x, y, 244, 137, 72, 40)) {
    if (streamerIsBusy()) streamerStop(); else grblSoftStop();
  }
  else if (hit(x, y, 4, 181, 74, 35)) grblZeroXY();
  else if (hit(x, y, 84, 181, 74, 35)) grblZeroZ();
  else if (hit(x, y, 164, 181, 74, 35)) { sys.page = PAGE_MANUAL; sys.needFullRedraw = true; }
  else if (hit(x, y, 244, 181, 72, 35)) grblUnlock();
}

void handleManualTap(int x, int y) {
  const int xs[3] = {4, 62, 120};
  const int ys[3] = {61, 104, 147};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      if (hit(x, y, xs[c], ys[r], 54, 38)) { manualJogFromCell(c, r); return; }
    }
  }
  if (hit(x, y, 180, 61, 46, 60)) grblJogZ(currentStep());
  else if (hit(x, y, 180, 127, 46, 58)) grblJogZ(-currentStep());
  else {
    if (hit(x, y, 237, 44, 76, 36)) {
      setStepIndex((sys.stepIndex + 1) % STEP_COUNT);
      return;
    }
    if (hit(x, y, 237, 106, 76, 36)) {
      setJogFeedIndex((sys.jogFeedIndex + 1) % JOG_FEED_COUNT);
      return;
    }
    if (hit(x, y, 4, 190, 70, 26)) grblZeroXY();
    else if (hit(x, y, 78, 190, 70, 26)) grblZeroZ();
    else if (hit(x, y, 152, 190, 74, 26)) grblUnlock();
  }
}

void handleFilesTap(int x, int y) {
  if (x < 207 && y >= 42 && y < 177) {
    int row = (y - 42) / 27;
    sdSelectIndex(sys.fileListOffset + row);
    return;
  }
  if (hit(x, y, 216, 28, 97, 27)) {
    if (sys.fileListOffset > 0) { --sys.fileListOffset; sys.needFullRedraw = true; }
  } else if (hit(x, y, 216, 60, 97, 27)) {
    if (sys.fileListOffset + 5 < sdFileCount()) { ++sys.fileListOffset; sys.needFullRedraw = true; }
  } else if (hit(x, y, 216, 92, 97, 27)) {
    sdRefreshFiles(); sys.needFullRedraw = true;
  } else if (hit(x, y, 216, 124, 97, 27)) {
    if (streamerStartSelected()) { sys.page = PAGE_DASHBOARD; sys.needFullRedraw = true; }
  } else if (hit(x, y, 216, 156, 97, 27)) {
    if (streamerIsBusy()) { showToast("NO SE PUEDE ELIMINAR EN MARCHA", C_ORANGE); return; }
    if ((int32_t)(millis() - deleteArmedUntil) < 0) {
      sdDeleteSelected();
      deleteArmedUntil = 0;
    } else {
      deleteArmedUntil = millis() + 3000;
      showToast("TOCA ELIMINAR OTRA VEZ", C_ORANGE, 3000);
    }
  }
}

void handleNetworkTap(int x, int y) {
  if (hit(x, y, 4, 163, 312, 24)) {
    networkReconnectGateway();
  } else if (hit(x, y, 4, 191, 153, 25)) {
    if (streamerIsBusy()) { showToast("DETENE LA MAQUINA PRIMERO", C_ORANGE); return; }
    if (touchRunCalibration(tft)) touchRunCoordinateTest(tft);
    sys.needFullRedraw = true;
  } else if (hit(x, y, 163, 191, 153, 25)) {
    if (streamerIsBusy()) { showToast("REINICIO BLOQUEADO EN MARCHA", C_ORANGE); return; }
    tft.fillScreen(C_RED);
    centeredText("REINICIANDO...", 0, 90, SCREEN_W, 50, C_WHITE, C_RED, 2);
    delay(400);
    ESP.restart();
  }
}
} // namespace

void uiInit() {
  tft.setTextWrap(false, false);
  tft.fillScreen(C_BLACK);
  sys.needFullRedraw = true;
}

void uiLoop() {
  if (sys.needFullRedraw) {
    drawFullPage();
    lastDynamicDraw = millis();
    return;
  }
  if (millis() - lastDynamicDraw >= 300) {
    lastDynamicDraw = millis();
    drawDynamic();
  }
}

void uiHandleTouch(const TouchEvent& event) {
  if (!event.tap) return;
  int x = event.x;
  int y = event.y;
  if (y >= TAB_Y) {
    handleTabs(x, y);
    return;
  }
  switch (sys.page) {
    case PAGE_DASHBOARD: handleDashboardTap(x, y); break;
    case PAGE_MANUAL: handleManualTap(x, y); break;
    case PAGE_FILES: handleFilesTap(x, y); break;
    case PAGE_NETWORK: handleNetworkTap(x, y); break;
  }
}

void uiForceRedraw() {
  sys.needFullRedraw = true;
}
