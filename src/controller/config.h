#pragma once
#include <Arduino.h>
#include "network_credentials.h"

// ============================================================
// CNCBAIRES - PANTALLA / CONTROL CNC
// Placa: ESP32-2432S028R (CYD, ILI9341, 320x240, XPT2046)
// Firmware: 3.1.0 DUAL ESP32 + ESP8266
// ============================================================

#define FW_NAME        "CNCBAIRES PANTALLA"
#define FW_VERSION     "3.1.0"
#define DEVICE_HOSTNAME "cncbaires-esp32"

// ---------------- Pantalla ----------------
#define SCREEN_W 320
#define SCREEN_H 240
#ifndef TFT_BL
  #define TFT_BL 21
#endif
#define TFT_ROTATION 1

// ---------------- Touch XPT2046 ----------------
// Se usa SPI por software para dejar libres los dos buses SPI de hardware:
// HSPI = TFT, VSPI = microSD.
#define TOUCH_CS    33
#define TOUCH_IRQ   36
#define TOUCH_CLK   25
#define TOUCH_MISO  39
#define TOUCH_MOSI  32
#define TOUCH_SAMPLE_COUNT 7
#define TOUCH_TAP_MAX_MS 900
#define TOUCH_MOVE_TOLERANCE 18

// ---------------- microSD (VSPI) ----------------
#define SD_CS    5
#define SD_SCLK  18
#define SD_MISO  19
#define SD_MOSI  23
#define SD_SPI_HZ 16000000UL
#define SD_MAX_FILES 40
#define SD_MAX_FILE_BYTES (32UL * 1024UL * 1024UL)

// ---------------- UART GRBL ----------------
// Conector CN1 de la ESP32-2432S028R:
// ESP32 RX27 <- TX de Arduino/GRBL (OBLIGATORIO bajar 5 V a 3.3 V)
// ESP32 TX22 -> RX de Arduino/GRBL.
// No usar P1 TX/RX: son GPIO1/3 y comparten el conversor USB CH340.
#define GRBL_RX 27
#define GRBL_TX 22
#define GRBL_BAUD 115200
#define GRBL_STATUS_POLL_MS 250
#define GRBL_TIMEOUT_MS 3500
#define GRBL_MAX_LINE_LEN 78  // GRBL 1.1 AVR usa por defecto 80 caracteres de linea.

// ---------------- Enlace privado con ESP8266 ----------------
// La ESP32 se conecta automaticamente a este AP. No conoce ni administra
// las credenciales del router del usuario; esa tarea pertenece al ESP8266.
#define GATEWAY_AP_SSID       CNC_AP_SSID
#define GATEWAY_AP_PASSWORD   CNC_AP_PASSWORD
#define ESP32_INTERNAL_HTTP_PORT 8080
#define ESP32_FILE_PORT       8890
#define ESP32_FILE_FRAME_MAX  4096UL
#define ESP32_FILE_TIMEOUT_MS 20000UL
#define ESP32_DIRECT_LINE_MAX 512U
#define GATEWAY_RETRY_MS      5000UL
#define GATEWAY_BOOT_WAIT_MS  15000UL
#define SPLASH_MIN_MS         5000UL

// ---------------- Jog ----------------
static constexpr float STEP_VALUES[] = {0.1f, 1.0f, 5.0f, 10.0f, 50.0f, 100.0f};
static constexpr uint8_t STEP_COUNT = sizeof(STEP_VALUES) / sizeof(STEP_VALUES[0]);
static constexpr uint16_t JOG_FEED_VALUES[] = {100, 250, 500, 1000, 1500, 2000};
static constexpr uint8_t JOG_FEED_COUNT = sizeof(JOG_FEED_VALUES) / sizeof(JOG_FEED_VALUES[0]);
static constexpr uint8_t DEFAULT_JOG_FEED_INDEX = 3; // 1000 mm/min

// ---------------- Interfaz: RGB565 ----------------
#define C_BLACK      0x0000
#define C_PANEL      0x1082
#define C_PANEL_2    0x18E3
#define C_BORDER     0xFFE0
#define C_YELLOW     0xFFE0
#define C_WHITE      0xFFFF
#define C_GRAY       0x8410
#define C_DARK_GRAY  0x4208
#define C_GREEN      0x07E0
#define C_ORANGE     0xFD20
#define C_RED        0xF800
#define C_BLUE       0x001F
#define C_CYAN       0x07FF

// ---------------- Seguridad / comportamiento ----------------
#define REQUIRE_GRBL_IDLE_TO_START 1
#define AUTO_ABORT_ON_SD_LOSS 1
#define AUTO_ABORT_ON_GRBL_LOSS 1
