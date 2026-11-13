#pragma once
#include <Arduino.h>

// En v3 la ESP32 no crea AP ni se conecta al router del usuario. Se conecta
// unicamente al AP privado del ESP8266 y expone una API interna para el gateway.
void networkInit();
void networkLoop();
void networkReconnectGateway();
bool networkGatewayConnected();
bool networkConsumeCalibrationRequest();
String networkUrl();
String networkExternalIp();
String networkLastWebEvent();
