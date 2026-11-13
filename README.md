# CNC Control

Dual-board firmware that connects a browser and a touch display to a GRBL CNC controller over a private Wi-Fi link.

![ESP32](https://img.shields.io/badge/ESP32-000000?logo=espressif&logoColor=white)
![ESP8266](https://img.shields.io/badge/ESP8266-E7352C?logo=espressif&logoColor=white)
![C++](https://img.shields.io/badge/C%2B%2B-00599C?logo=cplusplus&logoColor=white)
![PlatformIO](https://img.shields.io/badge/PlatformIO-F5822A?logo=platformio&logoColor=white)
![License](https://img.shields.io/badge/License-MIT-green)

## Features

- ESP32 CYD touch interface, microSD file management, and GRBL serial communication.
- ESP8266 access point, external Wi-Fi configuration, and embedded browser UI.
- Position/status reporting, manual jog, zero setting, pause/resume, and stop controls.
- G-code transfer to the ESP32 microSD with a READY handshake and chunk framing.
- Direct streaming through a temporary ESP8266 LittleFS file, with GRBL acknowledgement.
- Separate controller and gateway firmware targets.

**Hardware scope:** the source targets an ESP32-2432S028/CYD ILI9341 display,
a NodeMCU ESP8266, and a separate GRBL controller. There is no automatic homing
workflow; remote `$H` commands are blocked in the original design.
Firmware compilation does not verify wiring, movement, touch calibration, or
reliable cutting on a physical machine.

## Screenshots

**Screenshot 1:** `docs/screenshot-1.png`  
**Screenshot 2:** `docs/screenshot-2.png`

<!-- Screenshot slot: docs/screenshot-1.png — disconnected browser UI -->
<!-- Screenshot slot: docs/screenshot-2.png — operation tab -->
The included [read-only preview](docs/preview/index.html) is extracted from the
actual embedded page. Motion, file, and network controls are disabled. It is a
layout preview, not a simulator or a remote connection to a CNC.

## Build locally

Requirements: Python 3.10+, PlatformIO Core, and the listed hardware for flashing.

1. Download or clone the repository and open its root directory.
2. Create a virtual environment and install PlatformIO:

   ```bash
   python -m venv .venv
   # Linux/macOS:
   source .venv/bin/activate
   # Windows PowerShell instead: .venv/Scripts/Activate.ps1
   python -m pip install platformio==6.1.18
   ```

3. Copy `.env.example` to `.env` (`Copy-Item .env.example .env` in PowerShell).
4. Set a local AP name and password in `.env`. Both targets use these values.
   The example is a public demonstration value, not a production secret.
5. Generate the ignored credentials header and compile:

   ```bash
   python scripts/configure.py
   python -m platformio run
   ```

   The first build downloads the pinned ESP32/ESP8266 platforms and toolchains.
   Re-run `configure.py` after editing `.env`.

6. Flash one connected board at a time:

   ```bash
   python -m platformio run -e gateway -t upload
   python -m platformio run -e controller -t upload
   ```

7. Power both boards, connect to the AP configured in `.env`, and open
   <http://192.168.4.1>. The ESP32 uses `192.168.4.2` internally. Serial monitor:

   ```bash
   python -m platformio device monitor -b 115200
   ```

The two boards communicate over Wi-Fi. The ESP32 connects to GRBL through
RX27/TX22 at 115200 baud. See [hardware notes](docs/hardware.md) before connecting.

## Preview without hardware

```bash
python -m http.server 8080 --directory docs/preview
```

Open <http://localhost:8080>. The two tabs work; operational controls are disabled.
This static preview can be shared, but the firmware itself does not run on
GitHub Pages or Vercel.

## Structure

```text
src/controller/       ESP32 touch, SD, GRBL, streaming, and network code
src/gateway/          ESP8266 network firmware and embedded HTML asset
include/              Generated local credentials header (ignored)
lib/TFT_eSPI/         Original configured third-party display library and licenses
scripts/              Local configuration generator
docs/                 Protocol, hardware, validation, and read-only UI preview
platformio.ini        Pinned controller and gateway build environments
```

The web asset remains in `src/gateway/web_page.h` because it is compiled into flash.
The configured TFT library is retained to avoid silently changing display behavior.
Old binaries and release checksums are not distributed.

## What I learned

This project was practice in separating network work from display/control work,
handling acknowledgements and timeouts, and making file-transfer failures visible.
It also shows why successful builds still need physical hardware validation.

## Validation and license

See [validation](docs/validation.md), [protocol](docs/protocol.md), and
[public-edition changes](docs/public-edition.md). First-party firmware: [MIT](LICENSE).
The bundled display library and fonts retain their [original licenses](THIRD_PARTY_NOTICES.md).
