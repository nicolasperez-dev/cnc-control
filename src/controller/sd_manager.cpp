#include "sd_manager.h"
#include "config.h"
#include "state.h"
#include "gcode_streamer.h"
#include <SPI.h>
#include <SD.h>

namespace {
SPIClass sdSpi(VSPI);
SdFileInfo files[SD_MAX_FILES];
int fileCount = 0;
int selected = -1;
unsigned long lastHealthCheck = 0;

bool hasAllowedExtension(const String& value) {
  String lower = value;
  lower.toLowerCase();
  return lower.endsWith(".nc") || lower.endsWith(".gcode") || lower.endsWith(".tap") || lower.endsWith(".cnc");
}

void sortFiles() {
  for (int i = 0; i < fileCount - 1; ++i) {
    for (int j = i + 1; j < fileCount; ++j) {
      if (files[j].name.compareTo(files[i].name) < 0) {
        SdFileInfo temp = files[i];
        files[i] = files[j];
        files[j] = temp;
      }
    }
  }
}

bool mountCard() {
  if (sys.sdMounted) return true;
  sdSpi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSpi, SD_SPI_HZ)) {
    sys.sdMounted = false;
    sys.sdError = "No se pudo montar la microSD";
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    SD.end();
    sys.sdMounted = false;
    sys.sdError = "No hay tarjeta microSD";
    return false;
  }
  sys.sdMounted = true;
  sys.sdError = "";
  return true;
}
} // namespace

void sdManagerInit() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  if (mountCard()) {
    sdRefreshFiles();
    Serial.printf("[SD] montada, %d archivos G-code\n", fileCount);
  } else {
    Serial.printf("[SD] error: %s\n", sys.sdError.c_str());
  }
}

void sdManagerLoop() {
  if (millis() - lastHealthCheck < 3000) return;
  lastHealthCheck = millis();

  bool available = SD.cardType() != CARD_NONE;
  if (sys.sdMounted && !available) {
    sys.sdMounted = false;
    sys.sdError = "Tarjeta retirada";
    fileCount = 0;
    selected = -1;
    sys.needFullRedraw = true;
#if AUTO_ABORT_ON_SD_LOSS
    if (streamerUsesSd()) streamerAbortForFault("MicroSD retirada durante el trabajo");
#endif
  } else if (!sys.sdMounted) {
    SD.end();
    if (mountCard()) {
      sdRefreshFiles();
      showToast("MICROSD DETECTADA", C_GREEN);
      sys.needFullRedraw = true;
    }
  }
}

bool sdRefreshFiles() {
  fileCount = 0;
  String previouslySelected = selected >= 0 ? files[selected].name : String();
  selected = -1;

  if (!mountCard()) return false;
  fs::File root = SD.open("/");
  if (!root || !root.isDirectory()) {
    sys.sdError = "No se pudo leer la raiz de SD";
    return false;
  }

  fs::File entry = root.openNextFile();
  while (entry && fileCount < SD_MAX_FILES) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      if (name.startsWith("/")) name.remove(0, 1);
      if (hasAllowedExtension(name) && entry.size() <= SD_MAX_FILE_BYTES) {
        files[fileCount].name = name;
        files[fileCount].size = (uint32_t)entry.size();
        ++fileCount;
      }
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  sortFiles();

  if (previouslySelected.length()) sdSelectByName(previouslySelected);
  if (selected < 0 && fileCount > 0) selected = 0;
  sys.fileListOffset = constrain(sys.fileListOffset, 0, max(0, fileCount - 1));
  sys.sdError = "";
  return true;
}

bool sdIsMounted() { return sys.sdMounted; }
int sdFileCount() { return fileCount; }

const SdFileInfo* sdFileAt(int index) {
  if (index < 0 || index >= fileCount) return nullptr;
  return &files[index];
}

int sdSelectedIndex() { return selected; }

const String& sdSelectedName() {
  static String empty;
  if (selected < 0 || selected >= fileCount) return empty;
  return files[selected].name;
}

uint32_t sdSelectedSize() {
  if (selected < 0 || selected >= fileCount) return 0;
  return files[selected].size;
}

bool sdSelectIndex(int index) {
  if (index < 0 || index >= fileCount) return false;
  selected = index;
  if (selected < sys.fileListOffset) sys.fileListOffset = selected;
  if (selected >= sys.fileListOffset + 5) sys.fileListOffset = selected - 4;
  sys.needFullRedraw = true;
  return true;
}

bool sdSelectByName(const String& name) {
  String normalized = name;
  if (normalized.startsWith("/")) normalized.remove(0, 1);
  for (int i = 0; i < fileCount; ++i) {
    if (files[i].name.equalsIgnoreCase(normalized)) return sdSelectIndex(i);
  }
  return false;
}

bool sdDeleteSelected() {
  if (streamerIsBusy() || selected < 0 || selected >= fileCount || !sys.sdMounted) return false;
  String path = sdNormalizePath(files[selected].name);
  bool ok = SD.remove(path);
  if (ok) {
    String deleted = files[selected].name;
    sdRefreshFiles();
    showToast("ELIMINADO: " + deleted, C_ORANGE, 2200);
  } else {
    showToast("NO SE PUDO ELIMINAR", C_RED, 2200);
  }
  return ok;
}

fs::File sdOpenSelected(const char* mode) {
  if (selected < 0 || selected >= fileCount) return fs::File();
  return sdOpenPath(files[selected].name, mode);
}

fs::File sdOpenPath(const String& path, const char* mode) {
  if (!sys.sdMounted) return fs::File();
  String normalized = sdNormalizePath(path);
  return SD.open(normalized, mode);
}

bool sdIsAllowedGcodeName(const String& name) {
  if (!hasAllowedExtension(name)) return false;
  if (name.indexOf("..") >= 0 || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) return false;
  return true;
}

String sdNormalizePath(const String& name) {
  String safe = name;
  safe.trim();
  safe.replace("\\", "/");
  while (safe.startsWith("/")) safe.remove(0, 1);
  safe.replace("..", "");
  return "/" + safe;
}

String sdSanitizeUploadName(const String& original) {
  String base = original;
  base.replace("\\", "/");
  int slash = base.lastIndexOf('/');
  if (slash >= 0) base = base.substring(slash + 1);
  String out;
  out.reserve(48);
  for (size_t i = 0; i < base.length() && out.length() < 44; ++i) {
    char c = base.charAt(i);
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') out += c;
    else if (c == ' ') out += '_';
  }
  if (!hasAllowedExtension(out)) out += ".nc";
  if (out.length() < 4) out = "archivo.nc";
  return out;
}

uint64_t sdCardSizeBytes() {
  return sys.sdMounted ? SD.cardSize() : 0;
}

uint64_t sdUsedBytes() {
  return sys.sdMounted ? SD.usedBytes() : 0;
}
