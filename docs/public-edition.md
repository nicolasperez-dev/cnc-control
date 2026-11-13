# Public edition changes

- Consolidated the two PlatformIO projects into one root with two build targets.
- Moved application firmware to `src/controller` and `src/gateway`.
- Moved the configured TFT_eSPI copy to `lib/TFT_eSPI`, preserving its notices,
  driver setup, and original source. It is a dependency, not first-party code.
- Removed precompiled firmware, flash images, checksums, old release reports,
  and the unused alternative TFT setup file.
- Replaced AP literals with a shared ignored header generated from `.env`.
- Converted the sketch entry files to `main.cpp` so nested targets compile as standard C++ in PlatformIO.
- Kept the G-code parser, streaming, GRBL, touch, and networking logic.
- Added a disconnected HTML layout preview extracted from the original web asset.

No machine, physical movement, sensor response, or cutting result is simulated
or certified by this packaging work.
