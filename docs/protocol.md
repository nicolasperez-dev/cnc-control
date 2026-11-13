# Protocolo interno ESP8266–ESP32

La red privada usa `192.168.4.0/24`:

- ESP8266/gateway: `192.168.4.1`
- ESP32/pantalla: `192.168.4.2`

## API de control

El ESP32 escucha solo dentro del enlace privado en el puerto `8080`. El ESP8266 traduce las llamadas públicas `/api/...` a estas rutas internas:

- `GET /ping`
- `GET /status`
- `GET /files`
- `POST /select`
- `POST /delete`
- `POST /action`
- `POST /jog`
- `POST /command`

La API interna no implementa ninguna acción de HOME y rechaza `$H` en el comando manual.

## Transferencia de archivos

El ESP32 escucha en TCP `8890`.

1. El ESP8266 envía `PUT nombre.gcode\n`.
2. El ESP32 valida nombre, estado de máquina y microSD, abre `/.nombre.gcode.upload` y responde `READY|PUT|nombre.gcode`. Si falla, responde `ERR|motivo` antes de recibir datos.
3. Recién después de `READY`, cada bloque lleva cuatro bytes de longitud big-endian y los datos.
4. Un bloque con longitud cero finaliza el archivo.
5. El ESP32 cierra el temporal, lo renombra al nombre final y responde `OK|nombre|bytes` o `ERR|motivo`.

Los bloques admiten hasta 4096 bytes y se escriben directamente en la microSD.

## Corte directo sin microSD

1. La página guarda un temporal de hasta 768 KB en LittleFS del ESP8266.
2. El ESP8266 abre TCP 8890 y envía `RUN nombre.gcode bytes\n`.
3. El ESP32 exige GRBL conectado, estado Idle y ninguna ejecución activa; responde `READY|RUN|nombre.gcode` o `ERR|motivo`.
4. Cada trama contiene exactamente una línea fuente. El ESP32 la limpia, la envía a GRBL y responde `NEXT|linea|bytes` únicamente después del `ok` de GRBL.
5. La trama de longitud cero marca el final. Cuando GRBL vuelve a Idle, el ESP32 responde `OK|DIRECT|nombre|bytes`.
6. El ESP8266 borra el temporal al completar, detener o fallar. Una caída del enlace detiene la ejecución en el ESP32.

`POST /jog` admite además `feed`, expresado en mm/min y limitado a 50–5000.
