# Hardware notes

The source configuration targets the classic ESP32-2432S028/CYD (ILI9341 alternate
initialization), ESP32 Dev Module, and a NodeMCU ESP8266 with 4 MB flash.

| Connection | Pin/configuration |
| --- | --- |
| GRBL TX → ESP32 RX | GPIO27; reduce a 5 V GRBL TX signal to 3.3 V |
| ESP32 TX → GRBL RX | GPIO22 |
| GRBL serial | 115200 baud, common ground |
| TFT SPI | MISO12, MOSI13, SCLK14, CS15, DC2 |
| TFT backlight | GPIO21 |
| Touch | Separate SPI pins configured in `src/controller/config.h` |
| ESP8266 ↔ ESP32 | Private Wi-Fi; no UART data cable between these boards |

Use the existing board configuration only with matching hardware. Inspect pin
assignments in the source before flashing a different board. Test axis direction,
hold/resume/stop, SD transfers, and calibration with the tool disabled before a job.
The network endpoints are local control endpoints and do not implement Internet
authentication. Keep the controller on a trusted local network.

The original limits remain 32 MB for SD uploads and 768 KB for direct streaming.
The direct path waits for GRBL acknowledgement per line; it is not an arbitrary
high-throughput streaming implementation.
