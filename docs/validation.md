# Validation

Verified during public-edition preparation:

- The shared `.env` configuration generator ran with demonstration values.
- The embedded browser JavaScript passed a Node.js syntax check.
- The extracted preview contains no network requests; operational controls are
  disabled and tab-switching code is retained.
- Third-party display-library and font notices were preserved.
- Source packaging excludes generated credentials, firmware binaries, toolchains,
  and `.pio` output.

Firmware builds passed with PlatformIO Core 6.1.18:

| Target | Platform/framework | RAM | Flash |
| --- | --- | --- | --- |
| Controller | espressif32 6.12.0 / Arduino 2.0.17 | 49,440 / 327,680 bytes | 931,273 / 1,966,080 bytes |
| Gateway | espressif8266 4.2.1 / Arduino 3.1.2 | 33,360 / 81,920 bytes | 395,915 / 1,044,464 bytes |

Both standard C++ entry points and the relocated display library were included in
these builds. The included GitHub workflow compiles the same two targets.

No physical hardware, touch calibration, SD transfer, GRBL movement, or cutting test
was performed. Run the build and hardware checks in README.md before using a machine.
Screenshot filenames remain placeholders; the read-only HTML preview is included.
