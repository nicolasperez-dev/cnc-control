#pragma once
#include <Arduino.h>
#include "network_credentials.h"

#define FW_NAME            "CNCBAIRES GATEWAY"
#define FW_VERSION         "3.1.0"
#define DEVICE_HOSTNAME    "cncbaires"

// Red privada que usa la ESP32 automaticamente.
#define CONTROL_AP_SSID     CNC_AP_SSID
#define CONTROL_AP_PASSWORD CNC_AP_PASSWORD
#define CONTROL_AP_CHANNEL  6
#define CONTROL_AP_MAX_CLIENTS 4

#define WEB_PORT 80
#define ESP32_IP_1 192
#define ESP32_IP_2 168
#define ESP32_IP_3 4
#define ESP32_IP_4 2
#define ESP32_HTTP_PORT 8080
#define ESP32_FILE_PORT 8890

#define EXTERNAL_WIFI_TIMEOUT_MS 12000UL
#define RESTART_AFTER_SAVE_MS    3000UL
#define ESP32_HTTP_TIMEOUT_MS    4000UL
#define ESP32_FILE_REPLY_MS      10000UL
#define ESP32_FILE_WRITE_MS      8000UL
#define MAX_GCODE_BYTES          (32UL * 1024UL * 1024UL)

// El corte directo usa un archivo temporal en LittleFS del ESP8266. Se borra
// al completar, detener o fallar el trabajo; nunca se guarda en la microSD.
#define DIRECT_TEMP_PATH         "/direct.tmp"
#define DIRECT_MAX_GCODE_BYTES   (768UL * 1024UL)
#define DIRECT_SOURCE_LINE_MAX   512U
#define DIRECT_HANDSHAKE_MS      10000UL
#define DIRECT_REPLY_TIMEOUT_MS  (6UL * 60UL * 60UL * 1000UL)

#define EEPROM_BYTES 256
#define WIFI_CONFIG_MAGIC 0x434E4342UL  // "CNCB"
