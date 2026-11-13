#pragma once
#include <Arduino.h>
#include <FS.h>

struct SdFileInfo {
  String name;
  uint32_t size = 0;
};

void sdManagerInit();
void sdManagerLoop();
bool sdRefreshFiles();
bool sdIsMounted();
int sdFileCount();
const SdFileInfo* sdFileAt(int index);
int sdSelectedIndex();
const String& sdSelectedName();
uint32_t sdSelectedSize();
bool sdSelectIndex(int index);
bool sdSelectByName(const String& name);
bool sdDeleteSelected();
fs::File sdOpenSelected(const char* mode = FILE_READ);
fs::File sdOpenPath(const String& path, const char* mode = FILE_READ);
bool sdIsAllowedGcodeName(const String& name);
String sdNormalizePath(const String& name);
String sdSanitizeUploadName(const String& original);
uint64_t sdCardSizeBytes();
uint64_t sdUsedBytes();
