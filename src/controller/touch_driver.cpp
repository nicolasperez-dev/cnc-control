#include "touch_driver.h"
#include "config.h"
#include <Preferences.h>

namespace {
struct CalibrationData {
  uint32_t magic = 0;
  bool xUsesRawX = true;
  int32_t xRaw0 = 300;
  int32_t xRawMax = 3800;
  int32_t yRaw0 = 300;
  int32_t yRawMax = 3800;
};

constexpr uint32_t CAL_MAGIC = 0x43594432; // "CYD2"
CalibrationData cal;
Preferences prefs;

bool wasDown = false;
uint32_t pressStarted = 0;
int16_t pressX = 0;
int16_t pressY = 0;
int16_t lastX = 0;
int16_t lastY = 0;

uint8_t softTransfer(uint8_t out) {
  uint8_t in = 0;
  for (int bit = 7; bit >= 0; --bit) {
    digitalWrite(TOUCH_CLK, LOW);
    digitalWrite(TOUCH_MOSI, (out >> bit) & 0x01);
    delayMicroseconds(1);
    digitalWrite(TOUCH_CLK, HIGH);
    in = (uint8_t)((in << 1) | (digitalRead(TOUCH_MISO) ? 1 : 0));
    delayMicroseconds(1);
  }
  digitalWrite(TOUCH_CLK, LOW);
  return in;
}

uint16_t read12(uint8_t command) {
  softTransfer(command);
  uint16_t value = (uint16_t)softTransfer(0x00) << 8;
  value |= softTransfer(0x00);
  return value >> 3;
}

bool rawPressed() {
  return digitalRead(TOUCH_IRQ) == LOW;
}

bool readOne(uint16_t& rx, uint16_t& ry) {
  if (!rawPressed()) return false;
  digitalWrite(TOUCH_CS, LOW);
  delayMicroseconds(2);
  // Una lectura de descarte estabiliza el ADC resistivo.
  (void)read12(0xD0);
  rx = read12(0xD0);
  ry = read12(0x90);
  softTransfer(0x00);
  digitalWrite(TOUCH_CS, HIGH);
  return rawPressed() && rx > 100 && rx < 4095 && ry > 100 && ry < 4095;
}

void sortU16(uint16_t* values, int count) {
  for (int i = 1; i < count; ++i) {
    uint16_t key = values[i];
    int j = i - 1;
    while (j >= 0 && values[j] > key) {
      values[j + 1] = values[j];
      --j;
    }
    values[j + 1] = key;
  }
}

bool readStableRaw(uint16_t& rx, uint16_t& ry) {
  uint16_t xs[TOUCH_SAMPLE_COUNT];
  uint16_t ys[TOUCH_SAMPLE_COUNT];
  int count = 0;
  for (int i = 0; i < TOUCH_SAMPLE_COUNT + 3 && count < TOUCH_SAMPLE_COUNT; ++i) {
    uint16_t x = 0, y = 0;
    if (readOne(x, y)) {
      xs[count] = x;
      ys[count] = y;
      ++count;
    }
    delayMicroseconds(250);
  }
  if (count < 5) return false;
  sortU16(xs, count);
  sortU16(ys, count);
  if ((xs[count - 1] - xs[0]) > 650 || (ys[count - 1] - ys[0]) > 650) return false;
  rx = xs[count / 2];
  ry = ys[count / 2];
  return true;
}

int16_t mapAxis(int32_t raw, int32_t raw0, int32_t rawMax, int16_t maxScreen) {
  if (rawMax == raw0) return 0;
  float value = (float)(raw - raw0) * (float)maxScreen / (float)(rawMax - raw0);
  if (value < 0.0f) value = 0.0f;
  if (value > maxScreen) value = maxScreen;
  return (int16_t)lroundf(value);
}

bool readScreenPoint(int16_t& x, int16_t& y) {
  uint16_t rx = 0, ry = 0;
  if (!readStableRaw(rx, ry)) return false;
  int32_t rawForX = cal.xUsesRawX ? rx : ry;
  int32_t rawForY = cal.xUsesRawX ? ry : rx;
  x = mapAxis(rawForX, cal.xRaw0, cal.xRawMax, SCREEN_W - 1);
  y = mapAxis(rawForY, cal.yRaw0, cal.yRawMax, SCREEN_H - 1);
  return true;
}

void drawCross(TFT_eSPI& tft, int x, int y, uint16_t color) {
  tft.drawCircle(x, y, 9, color);
  tft.drawFastHLine(x - 15, y, 31, color);
  tft.drawFastVLine(x, y - 15, 31, color);
}

bool waitRawPoint(uint16_t& rx, uint16_t& ry, uint32_t timeoutMs) {
  uint32_t started = millis();
  while (!rawPressed()) {
    if (millis() - started > timeoutMs) return false;
    delay(5);
  }
  delay(80);

  uint32_t sx = 0, sy = 0;
  int valid = 0;
  for (int i = 0; i < 25; ++i) {
    uint16_t x = 0, y = 0;
    if (readStableRaw(x, y)) {
      sx += x;
      sy += y;
      ++valid;
    }
    delay(8);
  }
  while (rawPressed()) delay(5);
  if (valid < 10) return false;
  rx = (uint16_t)(sx / valid);
  ry = (uint16_t)(sy / valid);
  return true;
}

int32_t extrapolateEndpoint(int32_t rawA, int32_t rawB, int32_t screenA, int32_t screenB, int32_t target) {
  if (screenA == screenB) return rawA;
  return rawA + (int32_t)((int64_t)(target - screenA) * (rawB - rawA) / (screenB - screenA));
}

void saveCalibration() {
  prefs.begin("cnc-touch", false);
  prefs.putUInt("magic", CAL_MAGIC);
  prefs.putBool("xrawx", cal.xUsesRawX);
  prefs.putInt("xr0", cal.xRaw0);
  prefs.putInt("xrm", cal.xRawMax);
  prefs.putInt("yr0", cal.yRaw0);
  prefs.putInt("yrm", cal.yRawMax);
  prefs.end();
}

void loadCalibration() {
  prefs.begin("cnc-touch", true);
  cal.magic = prefs.getUInt("magic", 0);
  cal.xUsesRawX = prefs.getBool("xrawx", true);
  cal.xRaw0 = prefs.getInt("xr0", 300);
  cal.xRawMax = prefs.getInt("xrm", 3800);
  cal.yRaw0 = prefs.getInt("yr0", 300);
  cal.yRawMax = prefs.getInt("yrm", 3800);
  prefs.end();
}
} // namespace

void touchInit() {
  pinMode(TOUCH_CS, OUTPUT);
  pinMode(TOUCH_CLK, OUTPUT);
  pinMode(TOUCH_MOSI, OUTPUT);
  pinMode(TOUCH_MISO, INPUT);
  pinMode(TOUCH_IRQ, INPUT);
  digitalWrite(TOUCH_CS, HIGH);
  digitalWrite(TOUCH_CLK, LOW);
  digitalWrite(TOUCH_MOSI, LOW);
  loadCalibration();
  Serial.printf("[TOUCH] calibracion: %s\n", touchCalibrationValid() ? "OK" : "PENDIENTE");
}

bool touchCalibrationValid() {
  if (cal.magic != CAL_MAGIC) return false;
  if (abs(cal.xRawMax - cal.xRaw0) < 1200) return false;
  if (abs(cal.yRawMax - cal.yRaw0) < 1200) return false;
  return true;
}

void touchClearCalibration() {
  prefs.begin("cnc-touch", false);
  prefs.clear();
  prefs.end();
  cal.magic = 0;
}

bool touchBootCalibrationRequested(uint32_t holdMs) {
  if (!rawPressed()) return false;
  uint32_t start = millis();
  while (rawPressed()) {
    if (millis() - start >= holdMs) {
      while (rawPressed()) delay(5);
      return true;
    }
    delay(10);
  }
  return false;
}

TouchEvent touchPoll() {
  TouchEvent event;
  int16_t x = lastX, y = lastY;
  bool down = rawPressed() && touchCalibrationValid() && readScreenPoint(x, y);
  event.down = down;

  if (down) {
    lastX = x;
    lastY = y;
    event.x = x;
    event.y = y;
    if (!wasDown) {
      wasDown = true;
      pressStarted = millis();
      pressX = x;
      pressY = y;
      event.justPressed = true;
    }
    event.heldMs = millis() - pressStarted;
  } else {
    event.x = lastX;
    event.y = lastY;
    if (wasDown) {
      wasDown = false;
      event.justReleased = true;
      uint32_t held = millis() - pressStarted;
      int dx = abs((int)lastX - (int)pressX);
      int dy = abs((int)lastY - (int)pressY);
      event.tap = held <= TOUCH_TAP_MAX_MS && dx <= TOUCH_MOVE_TOLERANCE && dy <= TOUCH_MOVE_TOLERANCE;
      event.heldMs = held;
    }
  }
  return event;
}

bool touchRunCalibration(TFT_eSPI& tft) {
  const int margin = 24;
  const int sx[4] = {margin, SCREEN_W - 1 - margin, SCREEN_W - 1 - margin, margin};
  const int sy[4] = {margin, margin, SCREEN_H - 1 - margin, SCREEN_H - 1 - margin};
  uint16_t rx[4] = {0}, ry[4] = {0};

  tft.fillScreen(C_BLACK);
  tft.setTextColor(C_YELLOW, C_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 88);
  tft.print("CALIBRACION TOUCH");
  tft.setTextColor(C_WHITE, C_BLACK);
  tft.setTextSize(1);
  tft.setCursor(28, 122);
  tft.print("Toca el centro de cada cruz.");
  delay(900);

  for (int i = 0; i < 4; ++i) {
    tft.fillScreen(C_BLACK);
    tft.setTextColor(C_WHITE, C_BLACK);
    tft.setTextSize(1);
    tft.setCursor(8, 8);
    tft.printf("Punto %d/4", i + 1);
    drawCross(tft, sx[i], sy[i], C_YELLOW);
    if (!waitRawPoint(rx[i], ry[i], 30000)) {
      tft.fillScreen(C_RED);
      tft.setTextColor(C_WHITE, C_RED);
      tft.setTextSize(2);
      tft.setCursor(30, 105);
      tft.print("CALIBRACION FALLIDA");
      delay(1500);
      return false;
    }
  }

  int32_t leftRx = (rx[0] + rx[3]) / 2;
  int32_t rightRx = (rx[1] + rx[2]) / 2;
  int32_t leftRy = (ry[0] + ry[3]) / 2;
  int32_t rightRy = (ry[1] + ry[2]) / 2;
  cal.xUsesRawX = abs(rightRx - leftRx) >= abs(rightRy - leftRy);

  int32_t xLeft = cal.xUsesRawX ? leftRx : leftRy;
  int32_t xRight = cal.xUsesRawX ? rightRx : rightRy;
  int32_t topRawA = cal.xUsesRawX ? ry[0] : rx[0];
  int32_t topRawB = cal.xUsesRawX ? ry[1] : rx[1];
  int32_t bottomRawA = cal.xUsesRawX ? ry[2] : rx[2];
  int32_t bottomRawB = cal.xUsesRawX ? ry[3] : rx[3];
  int32_t yTop = (topRawA + topRawB) / 2;
  int32_t yBottom = (bottomRawA + bottomRawB) / 2;

  cal.xRaw0 = extrapolateEndpoint(xLeft, xRight, margin, SCREEN_W - 1 - margin, 0);
  cal.xRawMax = extrapolateEndpoint(xLeft, xRight, margin, SCREEN_W - 1 - margin, SCREEN_W - 1);
  cal.yRaw0 = extrapolateEndpoint(yTop, yBottom, margin, SCREEN_H - 1 - margin, 0);
  cal.yRawMax = extrapolateEndpoint(yTop, yBottom, margin, SCREEN_H - 1 - margin, SCREEN_H - 1);
  cal.magic = CAL_MAGIC;
  saveCalibration();

  tft.fillScreen(C_GREEN);
  tft.setTextColor(C_BLACK, C_GREEN);
  tft.setTextSize(2);
  tft.setCursor(55, 105);
  tft.print("TOUCH CALIBRADO");
  delay(1000);
  wasDown = false;
  return true;
}

void touchRunCoordinateTest(TFT_eSPI& tft, uint32_t timeoutMs) {
  if (!touchCalibrationValid()) return;
  wasDown = false;
  tft.fillScreen(C_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(C_YELLOW, C_BLACK);
  tft.setCursor(8, 8);
  tft.print("PRUEBA TOUCH: toca la pantalla");
  tft.drawRect(0, 0, SCREEN_W, SCREEN_H, C_WHITE);
  tft.fillRect(220, 210, 94, 24, C_RED);
  tft.setTextColor(C_WHITE, C_RED);
  tft.setCursor(241, 218);
  tft.print("SALIR");

  const uint32_t started = millis();
  while (millis() - started < timeoutMs) {
    TouchEvent event = touchPoll();
    if (event.down) {
      tft.fillRect(8, 22, 190, 14, C_BLACK);
      tft.setTextColor(C_WHITE, C_BLACK);
      tft.setCursor(8, 24);
      tft.printf("X=%d  Y=%d", event.x, event.y);
      tft.fillCircle(event.x, event.y, 3, C_CYAN);
      Serial.printf("[TOUCH TEST] X=%d Y=%d\n", event.x, event.y);
    }
    if (event.tap && event.x >= 220 && event.y >= 210) break;
    delay(5);
  }
  wasDown = false;
}
