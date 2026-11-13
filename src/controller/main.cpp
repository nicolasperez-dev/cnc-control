#include <Arduino.h>
#include <SPI.h>
#include "TFT_eSPI.h"

#if !defined(ILI9341_2_DRIVER)
  #error "La copia integrada de TFT_eSPI no cargo User_Setup.h"
#endif
#if !defined(TFT_MOSI) || (TFT_MOSI != 13) || !defined(TFT_SCLK) || (TFT_SCLK != 14) || !defined(TFT_CS) || (TFT_CS != 15) || !defined(TFT_DC) || (TFT_DC != 2)
  #error "Pines TFT incorrectos para ESP32-2432S028R"
#endif

#include "config.h"
#include "state.h"
#include "touch_driver.h"
#include "sd_manager.h"
#include "gcode_streamer.h"
#include "grbl_comm.h"
#include "network_manager.h"
#include "ui.h"

TFT_eSPI tft = TFT_eSPI();

void drawBootSplash(const String& status) {
  tft.fillScreen(C_BLACK);
  tft.drawRoundRect(12, 18, 296, 204, 12, C_YELLOW);
  tft.drawRoundRect(15, 21, 290, 198, 10, C_DARK_GRAY);
  tft.setTextWrap(false, false);
  tft.setTextSize(3);
  tft.setTextColor(C_YELLOW, C_BLACK);
  tft.setCursor(79, 79);
  tft.print("CNCBAIRES");
  tft.drawFastHLine(64, 113, 192, C_YELLOW);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_BLACK);
  int x = max(10, (SCREEN_W - (int)status.length() * 6) / 2);
  tft.setCursor(x, 139);
  tft.print(status);
  tft.setTextColor(C_GRAY, C_BLACK);
  tft.setCursor(111, 182);
  tft.print("Firmware " FW_VERSION);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.printf("[BOOT] %s firmware %s\n", FW_NAME, FW_VERSION);
  stateInit();

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
  tft.init();
  tft.setRotation(TFT_ROTATION);
  if (tft.width() != SCREEN_W || tft.height() != SCREEN_H) {
    Serial.printf("[TFT] geometria incorrecta %dx%d\n", tft.width(), tft.height());
    digitalWrite(TFT_BL, HIGH);
    return;
  }
  tft.fillScreen(C_BLACK);
  digitalWrite(TFT_BL, HIGH);
  drawBootSplash("Preparando pantalla tactil...");

  touchInit();
  bool forceCalibration = touchBootCalibrationRequested();
  if (!touchCalibrationValid() || forceCalibration) {
    if (touchRunCalibration(tft)) touchRunCoordinateTest(tft);
  }

  // El splash se vuelve a dibujar despues de una eventual calibracion y se
  // mantiene como minimo cinco segundos o hasta terminar la espera del enlace.
  unsigned long splashStarted = millis();
  drawBootSplash("Montando microSD...");
  sdManagerInit();
  streamerInit();

  drawBootSplash("Conectando controlador GRBL...");
  grblInit();

  drawBootSplash("Buscando modulo ESP8266...");
  networkInit();

  unsigned long lastSplashUpdate = 0;
  while ((millis() - splashStarted < SPLASH_MIN_MS) ||
         (!networkGatewayConnected() && millis() - splashStarted < GATEWAY_BOOT_WAIT_MS)) {
    networkLoop();
    grblLoop();
    if (millis() - lastSplashUpdate >= 450) {
      lastSplashUpdate = millis();
      if (networkGatewayConnected()) drawBootSplash("ESP8266 conectado - iniciando...");
      else drawBootSplash(String("Esperando red ") + GATEWAY_AP_SSID + "...");
    }
    delay(2);
  }

  uiInit();
  showToast(networkGatewayConnected() ? "SISTEMA LISTO / ESP8266 OK" : "SISTEMA LISTO / BUSCANDO 8266",
            networkGatewayConnected() ? C_GREEN : C_ORANGE, 2600);
  Serial.println("[SYS] inicializacion completa");
}

void loop() {
  networkLoop();
  grblLoop();
  streamerLoop();
  sdManagerLoop();

  if (networkConsumeCalibrationRequest() && !streamerIsBusy()) {
    touchRunCalibration(tft);
    uiForceRedraw();
  }

  TouchEvent touch = touchPoll();
  uiHandleTouch(touch);
  stateLoop();
  uiLoop();
  networkLoop();
  delay(1);
}
