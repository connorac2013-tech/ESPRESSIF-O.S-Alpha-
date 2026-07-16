#include <EEPROM.h>
#include <Preferences.h>
#include "lwip/lwip_napt.h"
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <nvs_flash.h>
#include <ctype.h>
#include <math.h>
#include <strings.h>
#include <time.h>
#include <HardwareSerial.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include <LittleFS.h>
#include "driver/temperature_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern "C" {
#include "lwip/lwip_napt.h"
}
Preferences prefs;
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   27
#define I2C_SDA 33
#define I2C_SCL 32
#define TP_RST  25
#define TP_INT  21
#define BOOT_BTN_PIN 0
SPIClass spi(HSPI);
uint16_t themeBg = 0x0008, themeTile = 0x2104, themeAccent = 0x001F, themeText = 0xFFFF;
int currentTheme = 1;
uint16_t customBg = 0x0000;
uint16_t customTile = 0x4208;
uint16_t customAccent = 0x001F;
bool cornerMode = true;
bool aeroMode = false;
bool powerSaverEnabled = false;
void applyTheme(int idx) {
currentTheme = idx; themeText = 0xFFFF;
switch (idx) {
case 0: themeBg = 0x2001; themeTile = 0x6042; themeAccent = 0xB000; break;
case 2: themeBg = 0x2100; themeTile = 0x5282; themeAccent = 0x5320; break;
case 3: themeBg = 0x3186; themeTile = 0x738E; themeAccent = 0x738E; break;
case 4: themeBg = 0x1101; themeTile = 0x33C3; themeAccent = 0x2600; break;
case 5: themeBg = 0x0410; themeTile = 0xC618; themeAccent = 0x0010; break;
case 6: themeBg = 0x0000; themeTile = 0x39C7; themeAccent = 0x2104; break;
case 7: themeBg = 0xD69A; themeTile = 0x8410; themeAccent = 0x047D; themeText = 0x0000; break;
case 8: themeBg = 0x08A5; themeTile = 0x1148; themeAccent = 0x2B99; break;
case 9: themeBg = customBg; themeTile = customTile; themeAccent = customAccent; break;
default: themeBg = 0x0008; themeTile = 0x2104; themeAccent = 0x001F; currentTheme = 1; break;
}
}
bool pinEnabled = false;
char pinCode[5] = "";
int brightness = 255;
bool brightnessDirty = false;
#define BL_PWM_FREQ 5000
#define BL_PWM_RES 8
void applyBrightness();
int heaterLowF = 99, heaterMaxF = 105;
float readChipTempC() {
float t = temperatureRead();
if (t < -40 || t > 125) return 25.0;
return t - 35.0;
}
#define COLOR_WHITE 0xFFFF
#define COLOR_BLACK 0x0000
#define COLOR_RED 0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_BLUE 0x001F
#define COLOR_GRAY 0x4208
#define COLOR_YELLOW 0xFFE0
#define COLOR_PURPLE 0x780F
#define COLOR_BROWN 0xA145
#define COLOR_CYAN 0x07FF
#define COLOR_PINK 0xF81F
#define COLOR_SKY 0x5D9F
#define COLOR_WOOD 0xD5A0
#define COLOR_ORANGE 0xFD20
#define COLOR_DARK_ORANGE 0xFC00
#define COLOR_LIGHT_ORANGE 0xFE40
#define COLOR_GOLD 0xFEA0
int globalState = 0;
bool global_wasTouched = false;
unsigned long homePressStart = 0;
bool homeHoldTriggered = false;
bool recoveryMode = false;
int recoveryState = 0;
bool overlayEnabled = true;
int overlayX = 80, overlayY = 0;
bool overlayDragging = false;
int overlayDragStartX = 0, overlayDragStartY = 0;
unsigned long fpsLastFrameTime = 0, fpsDisplayTime = 0;
int fpsDisplay = 0, genFpsDisplay = 0, cpuDisplay = 0, gpuDisplay = 0;
int frameCounter = 0, genFrameCounter = 0;
unsigned long gpuWorkAccum = 0;
bool cpu_metric = true;
volatile bool heaterActive = false;
float heaterLastTempF = 0;
unsigned long heaterLastCheck = 0;
TaskHandle_t cpuTaskHandle;

void drawFpsOverlay() {
if (!overlayEnabled) return;
uint16_t bg = (currentTheme == 8) ? 0x1148 : themeTile;
fillRect(overlayX, overlayY, 160, 32, bg);
drawLine(overlayX, overlayY, overlayX + 159, overlayY, COLOR_WHITE);
drawLine(overlayX, overlayY + 31, overlayX + 159, overlayY + 31, COLOR_WHITE);
drawLine(overlayX, overlayY, overlayX, overlayY + 31, COLOR_WHITE);
drawLine(overlayX + 159, overlayY, overlayX + 159, overlayY + 31, COLOR_WHITE);
char buf1[32], buf2[32], buf3[32];
snprintf(buf1, sizeof(buf1), "%dFPS CPU %dMHz", fpsDisplay, (int)getCpuFrequencyMhz());
snprintf(buf2, sizeof(buf2), "CPU:%d%% GPU:%d%%", cpuDisplay, gpuDisplay);
float c = readChipTempC();
float displayTemp = cpu_metric ? c : (c * 9.0f / 5.0f + 32.0f);
snprintf(buf3, sizeof(buf3), "TEMP:%.1f%s", displayTemp, cpu_metric ? "C" : "F");

float tempF = c * 9.0f / 5.0f + 32.0f;
uint16_t tempColor = COLOR_GREEN;
bool blinkTemp = false;

if (tempF > heaterMaxF + 10) tempColor = COLOR_RED;
if (tempF > heaterMaxF + 20) blinkTemp = true;
if (tempF < heaterLowF - 10) tempColor = COLOR_BLUE;

drawText5x7(overlayX + 4, overlayY + 3, buf1, COLOR_GREEN);
drawText5x7(overlayX + 4, overlayY + 12, buf2, COLOR_GREEN);

if (blinkTemp) {
    if ((millis() / 250) % 2 == 0) {
        drawText5x7(overlayX + 4, overlayY + 21, buf3, COLOR_RED);
    } else {
        drawText5x7(overlayX + 4, overlayY + 21, buf3, COLOR_YELLOW);
    }
} else {
    drawText5x7(overlayX + 4, overlayY + 21, buf3, tempColor);
}
}
void updateFpsCpu() {
unsigned long now = micros();
frameCounter++;
genFrameCounter++;
fpsLastFrameTime = now;
if (millis() - fpsDisplayTime >= 1000) {
fpsDisplay = frameCounter;
genFpsDisplay = genFrameCounter;
frameCounter = 0;
genFrameCounter = 0;
fpsDisplayTime = millis();
gpuDisplay = (gpuWorkAccum + 5000) / 10000;
if (gpuDisplay > 100) gpuDisplay = 100;
gpuWorkAccum = 0;
if (overlayEnabled) drawFpsOverlay();
}
}
bool readTouch(int16_t &x, int16_t &y);
void recovery_drawMenu();
void recovery_touch(int16_t x, int16_t y);
void drawLauncherScreen();
void wifi_reset();
void fireworks_reset();
void reset_allSettings();
void reset_clearWifi();
void drawHomeButton();
void drawMoreButton();
void logWin(const char *msg);
void tft_cmd(uint8_t cmd) { digitalWrite(TFT_DC, LOW); digitalWrite(TFT_CS, LOW); spi.write(cmd); digitalWrite(TFT_CS, HIGH); }
void tft_data(uint8_t data) { digitalWrite(TFT_DC, HIGH); digitalWrite(TFT_CS, LOW); spi.write(data); digitalWrite(TFT_CS, HIGH); }
void tft_data16(uint16_t data) { digitalWrite(TFT_DC, HIGH); digitalWrite(TFT_CS, LOW); spi.write16(data); digitalWrite(TFT_CS, HIGH); }
void tft_setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
tft_cmd(0x2A); tft_data(x >> 8); tft_data(x & 0xFF); tft_data((x + w - 1) >> 8); tft_data((x + w - 1) & 0xFF);
tft_cmd(0x2B); tft_data(y >> 8); tft_data(y & 0xFF); tft_data((y + h - 1) >> 8); tft_data((y + h - 1) & 0xFF);
tft_cmd(0x2C);
}
void drawPixel(uint16_t x, uint16_t y, uint16_t color) {
if (x >= 320 || y >= 240) return;
digitalWrite(TFT_CS, LOW); digitalWrite(TFT_DC, LOW); spi.write(0x2A);
digitalWrite(TFT_DC, HIGH); spi.write16(x); spi.write16(x);
digitalWrite(TFT_DC, LOW); spi.write(0x2B);
digitalWrite(TFT_DC, HIGH); spi.write16(y); spi.write16(y);
digitalWrite(TFT_DC, LOW); spi.write(0x2C);
digitalWrite(TFT_DC, HIGH); spi.write16(color);
digitalWrite(TFT_CS, HIGH);
}
void tft_fillCircle(int cx, int cy, int r, uint16_t color) {
for (int y = -r; y <= r; y++)
for (int x = -r; x <= r; x++)
if (x*x + y*y <= r*r)
drawPixel(cx + x, cy + y, color);
}
void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
if (x >= 320 || y >= 240) return;
if (x + w > 320) w = 320 - x; if (y + h > 240) h = 240 - y;
tft_setAddrWindow(x, y, w, h);
digitalWrite(TFT_DC, HIGH); digitalWrite(TFT_CS, LOW);
uint16_t buf[256]; for (int i = 0; i < 256; i++) buf[i] = color;
uint32_t count = (uint32_t)w * h;
while (count > 0) {
int chunk = (count > 256) ? 256 : count;
spi.writePixels(buf, chunk * 2);
count -= chunk;
}
digitalWrite(TFT_CS, HIGH);
}
void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
if (r <= 0) { fillRect(x, y, w, h, color); return; }
if (r * 2 > w) r = w / 2;
if (r * 2 > h) r = h / 2;
fillRect(x + r, y, w - 2 * r, h, color);
fillRect(x, y + r, r, h - 2 * r, color);
fillRect(x + w - r, y + r, r, h - 2 * r, color);
for (int dy = -r; dy <= 0; dy++) {
for (int dx = -r; dx <= 0; dx++) {
if (dx*dx + dy*dy <= r*r) {
drawPixel(x + r + dx, y + r + dy, color);
drawPixel(x + w - 1 - r - dx, y + r + dy, color);
drawPixel(x + r + dx, y + h - 1 - r - dy, color);
drawPixel(x + w - 1 - r - dx, y + h - 1 - r - dy, color);
}
}
}
}
void fillScreen(uint16_t color) {
tft_setAddrWindow(0, 0, 320, 240);
digitalWrite(TFT_DC, HIGH); digitalWrite(TFT_CS, LOW);
uint16_t buf[256]; for (int i = 0; i < 256; i++) buf[i] = color;
int remaining = 320 * 240;
while (remaining > 0) {
int chunk = (remaining > 256) ? 256 : remaining;
spi.writePixels(buf, chunk * 2);
remaining -= chunk;
}
digitalWrite(TFT_CS, HIGH);
}
void fillScreenTheme() {
if (currentTheme != 8) { fillScreen(themeBg); return; }
tft_setAddrWindow(0, 0, 320, 240);
digitalWrite(TFT_DC, HIGH); digitalWrite(TFT_CS, LOW);
uint16_t buf[320];
for (int y = 0; y < 240; y++) {
float t = y / 240.0;
int r = (int)(20 + (8 - 20) * t), g = (int)(120 + (35 - 120) * t), b = (int)(105 + (85 - 105) * t);
uint16_t color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
for (int x = 0; x < 320; x++) buf[x] = color;
spi.writePixels(buf, 320 * 2);
}
digitalWrite(TFT_CS, HIGH);
}
void drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
int err = dx + dy, e2;
while (true) {
drawPixel(x0, y0, color);
if (x0 == x1 && y0 == y1) break;
e2 = 2 * err;
if (e2 >= dy) { err += dy; x0 += sx; }
if (e2 <= dx) { err += dx; y0 += sy; }
}
}
void applyBrightness() { ledcWrite(TFT_BL, brightness); }
void ili9341_init() {
delay(300);
pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
int16_t rx, ry;
bool bootBtnHeld = (digitalRead(BOOT_BTN_PIN) == LOW);
if (readTouch(rx, ry) || bootBtnHeld) {
recoveryMode = true; recovery_drawMenu();
while(recoveryMode) { if(readTouch(rx, ry)) { recovery_touch(rx, ry); while(readTouch(rx, ry)) delay(10); } }
}
pinMode(TFT_CS, OUTPUT); pinMode(TFT_DC, OUTPUT); pinMode(TFT_BL, OUTPUT);
ledcAttach(TFT_BL, BL_PWM_FREQ, BL_PWM_RES); applyBrightness();
spi.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
spi.beginTransaction(SPISettings(80000000, MSBFIRST, SPI_MODE0));
delay(50);
tft_cmd(0x01); delay(120); tft_cmd(0x36); tft_data(0xA0); tft_cmd(0x3A); tft_data(0x55);
tft_cmd(0x11); delay(120); tft_cmd(0x29);
}
bool readTouch(int16_t &x, int16_t &y) {
Wire.beginTransmission(0x15); Wire.write(0x02);
if (Wire.endTransmission() != 0) return false;
Wire.requestFrom(0x15, 5);
if (Wire.available() < 5) return false;
uint8_t points = Wire.read();
if (points == 0 || points > 5) return false;
uint8_t xHi = Wire.read(), xLo = Wire.read(), yHi = Wire.read(), yLo = Wire.read();
uint16_t raw_x = ((xHi & 0x0F) << 8) | xLo;
uint16_t raw_y = ((yHi & 0x0F) << 8) | yLo;
x = 320 - raw_y; y = raw_x;
return true;
}
void drawHomeButton() {
fillRoundRect(5, 5, 32, 32, cornerMode ? 3 : 0, COLOR_GRAY);
drawLine(12, 21, 28, 21, COLOR_WHITE); drawLine(12, 21, 20, 13, COLOR_WHITE); drawLine(12, 21, 20, 29, COLOR_WHITE);
}
bool isHomeTouched(int16_t x, int16_t y) { return (x <= 40 && y <= 45); }
void drawMoreButton() {
fillRoundRect(283, 5, 32, 32, cornerMode ? 3 : 0, COLOR_GRAY);
fillRect(295, 13, 8, 2, COLOR_WHITE); fillRect(295, 21, 8, 2, COLOR_WHITE); fillRect(295, 29, 8, 2, COLOR_WHITE);
}
bool isMoreTouched(int16_t x, int16_t y) { return (x >= 283 && x <= 315 && y >= 5 && y <= 37); }
const uint8_t font5x7[][5] PROGMEM = {
{0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},{0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},{0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},{0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},{0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44}
};
void drawChar5x7(int x, int y, char c, uint16_t color) {
if (c < 32 || c > 122) return;
const uint8_t *glyph = font5x7[c - 32];
for (int col = 0; col < 5; col++) {
uint8_t line = pgm_read_byte(&glyph[col]);
for (int row = 0; row < 7; row++) if (line & (1 << row)) drawPixel(x + col, y + row, color);
}
}
void drawText5x7(int x, int y, const char *str, uint16_t color) { while (*str) { drawChar5x7(x, y, *str, color); x += 6; str++; } }
void drawChar5x7Scaled(int x, int y, char c, uint16_t color, int scale) {
if (c < 32 || c > 122) return;
const uint8_t *glyph = font5x7[c - 32];
for (int col = 0; col < 5; col++) {
uint8_t line = pgm_read_byte(&glyph[col]);
for (int row = 0; row < 7; row++) if (line & (1 << row)) fillRect(x + col * scale, y + row * scale, scale, scale, color);
}
}
void drawText5x7Scaled(int x, int y, const char *str, uint16_t color, int scale) { while (*str) { drawChar5x7Scaled(x, y, *str, color, scale); x += 6 * scale; str++; } }
void drawSplashBadge(int cx, int cy) {
fillRect(0, 0, 50, 30, COLOR_BLACK);
drawText5x7Scaled(2, 0, "SOFT O.S", COLOR_CYAN, 1); drawText5x7Scaled(5, 18, "BIOS", COLOR_CYAN, 2);
drawText5x7Scaled(185, 230, "POWERED BY ESP_CAPABLE", COLOR_BLUE, 1); drawText5x7Scaled(11, 230, "ESP32 Inside", COLOR_RED, 1);
int radii[4] = { 18, 30, 42, 54 }; int thickness = 9;
for (int i = 0; i < 4; i++) {
int r = radii[i], rInner = r - thickness;
for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
int distSq = dx * dx + dy * dy;
if (distSq > r * r || distSq < rInner * rInner) continue;
float deg = atan2((float)dy, (float)dx) * 180.0 / PI;
if (deg > 95 && deg < 185) continue;
drawPixel(cx + dx, cy + dy, COLOR_RED);
}
}
delay(100); fillRect(cx - 42, cy + 28, 8, 8, COLOR_RED);
}
bool isbootoptionsTouched(int16_t x, int16_t y) { return (x <= 50 && y <= 30); }
bool isCrashBarTouched(int16_t x, int16_t y) { return (y <= 34); }
void drawSplashScreen() {
int16_t x, y;
fillScreen(COLOR_BLACK); drawSplashBadge(160, 80); delay(100);
drawText5x7Scaled(82, 148, "ESPRESSIF O.S", COLOR_RED, 2); delay(100);
drawText5x7(118, 172, "Empowering all", COLOR_WHITE);
for (int i = 0; i < 320; i++) {
if (readTouch(x, y) && isbootoptionsTouched(x, y)) {
while (readTouch(x, y)) delay(10);
recoveryMode = true; recovery_drawMenu();
while (recoveryMode) { if (readTouch(x, y)) { recovery_touch(x, y); while (readTouch(x, y)) delay(10); } }
return;
}
fillRect(0, 200, i, 20, COLOR_RED); delay(1);
}
EEPROM.begin(512);
int crashCode = EEPROM.read(7);
if (crashCode == 1 || crashCode == 6) {
fillScreen(COLOR_RED); fillRect(0, 0, 320, 34, COLOR_WHITE);
drawText5x7Scaled(8, 9, "SOFT O.S BIOS", COLOR_BLACK, 2); drawText5x7Scaled(12, 48, ":(", COLOR_WHITE, 5);
drawText5x7Scaled(75, 55, "CRASH REPORT", COLOR_WHITE, 2); drawLine(10, 95, 310, 95, COLOR_WHITE);
drawText5x7(10, 108, "YOUR DEVICE RESTARTED", COLOR_WHITE);
if (crashCode == 6) {
drawText5x7(10, 122, "DUE TO AN OVERHEAT ISSUE.", COLOR_WHITE);
drawText5x7(10, 145, "ERR CODE 6: OVERHEAT", COLOR_YELLOW);
} else {
drawText5x7(10, 122, "DUE TO A POWER ISSUE.", COLOR_WHITE);
drawText5x7(10, 145, "ERR CODE 7: UNEXPECTED SHUTDOWN", COLOR_YELLOW);
}
drawText5x7(10, 180, "TAP TOP BAR FOR RECOVERY OPTIONS", COLOR_WHITE);
for (int C = 0; C < 320; C++) {
if (readTouch(x, y) && isCrashBarTouched(x, y)) {
while (readTouch(x, y)) delay(10); recoveryMode = true; recovery_drawMenu();
while (recoveryMode) { if (readTouch(x, y)) { recovery_touch(x, y); while (readTouch(x, y)) delay(10); } }
return;
}
fillRect(0, 220, C, 20, COLOR_BLUE); delay(10);
}
}
EEPROM.write(7, 0); EEPROM.commit();
}
float heater_readTempF() { return readChipTempC() * 9.0f / 5.0f + 32.0f; }
void condensation_warn() {
int16_t x, y; float currentTemp = heater_readTempF();
fillScreen(COLOR_YELLOW); fillRect(0, 0, 320, 34, COLOR_BLACK);
drawText5x7Scaled(8, 9, "SOFT O.S BIOS", COLOR_WHITE, 2); drawText5x7Scaled(12, 48, "!!", COLOR_BLACK, 5);
drawText5x7Scaled(75, 55, "CONDENSATION ALERT", COLOR_BLACK, 2); drawLine(10, 95, 310, 95, COLOR_BLACK);
drawText5x7(10, 108, "CHIP TEMPERATURE TOO LOW", COLOR_BLACK); drawText5x7(10, 122, "CONDENSATION RISK DETECTED.", COLOR_BLACK);
char buf[48]; snprintf(buf, sizeof(buf), "CURRENT: %.1fF  LIMIT: %dF", currentTemp, heaterLowF - 20);
drawText5x7(10, 145, buf, COLOR_BLACK); fillRoundRect(60, 168, 200, 26, cornerMode?3:0, COLOR_RED);
drawText5x7(68, 176, "BYPASS AND ACCEPT RISK", COLOR_WHITE); drawText5x7(10, 205, "TAP TOP BAR FOR RECOVERY OPTIONS", COLOR_BLACK);
for (int C = 0; C < 320; C++) {
if (readTouch(x, y)) {
if (isCrashBarTouched(x, y)) { while (readTouch(x, y)) delay(10); recoveryMode = true; recovery_drawMenu(); while (recoveryMode) { if (readTouch(x, y)) { recovery_touch(x, y); while (readTouch(x, y)) delay(10); } } return; }
if (x >= 60 && x <= 260 && y >= 168 && y <= 194) { while (readTouch(x, y)) delay(10); return; }
}
fillRect(0, 220, C, 20, COLOR_BLACK); delay(10);
}
while (true) {
if (readTouch(x, y)) {
if (isCrashBarTouched(x, y)) { while (readTouch(x, y)) delay(10); recoveryMode = true; recovery_drawMenu(); while (recoveryMode) { if (readTouch(x, y)) { recovery_touch(x, y); while (readTouch(x, y)) delay(10); } } return; }
if (x >= 60 && x <= 260 && y >= 168 && y <= 194) { while (readTouch(x, y)) delay(10); return; }
}
delay(50);
}
}
void drawMiniLogo(int cx, int cy) {
int radii[4] = { 6, 10, 14, 18 };
int thickness = 3;
for (int i = 0; i < 4; i++) {
int r = radii[i];
int rInner = r - thickness;
for (int dy = -r; dy <= r; dy++) {
for (int dx = -r; dx <= r; dx++) {
int distSq = dx * dx + dy * dy;
if (distSq > r * r || distSq < rInner * rInner) continue;
float deg = atan2((float)dy, (float)dx) * 180.0 / PI;
if (deg > 95 && deg < 185) continue;
drawPixel(cx + dx, cy + dy, COLOR_RED);
}
}
}
fillRect(cx - 14, cy + 9, 3, 3, COLOR_RED);
}
void recovery_drawMenu() {
recoveryState = 0; fillScreen(COLOR_BLACK); EEPROM.begin(512);
int crashCode = EEPROM.read(7);
if (crashCode == 1 || crashCode == 6) { 
fillRoundRect(20, 210, 280, 20, cornerMode?3:0, COLOR_RED); 
if (crashCode == 6) drawText5x7(25, 215, "device crashed due to overheat", COLOR_WHITE);
else drawText5x7(25, 215, "your device restarted due to a power issue", COLOR_WHITE);
EEPROM.write(7, 0); EEPROM.commit(); 
}
drawText5x7(30, 20, "Soft O.S BIOS RECOVERY options", COLOR_WHITE); drawMiniLogo(295, 20);
fillRoundRect(20, 70, 280, 35, cornerMode?3:0, COLOR_GRAY); drawText5x7(22, 78, "RESET ALL SETTINGS", COLOR_WHITE); drawText5x7(22, 92, "(this action can not be undone)", COLOR_WHITE);
fillRoundRect(20, 115, 280, 35, cornerMode?3:0, COLOR_GRAY); drawText5x7(22, 123, "CLEAR SAVED WIFI", COLOR_WHITE); drawText5x7(22, 137, "(this action can not be undone)", COLOR_WHITE);
fillRoundRect(20, 160, 280, 35, cornerMode?3:0, COLOR_GRAY); drawText5x7(130, 175, "CANCEL", COLOR_WHITE);
}
void recovery_drawConfirm(const char *label) {
fillScreen(COLOR_BLACK); drawMiniLogo(295, 20); drawText5x7(20, 20, "ARE YOU SURE?", COLOR_WHITE);
char line[40]; snprintf(line, sizeof(line), "RESET %s SETTINGS?", label);
drawText5x7(20, 55, line, COLOR_WHITE); drawText5x7(20, 70, "ATTENTION: THIS ACTION CAN NOT BE UNDONE", COLOR_WHITE);
fillRoundRect(40, 120, 110, 45, cornerMode?3:0, COLOR_RED); drawText5x7(78, 138, "YES", COLOR_WHITE);
fillRoundRect(170, 120, 110, 45, cornerMode?3:0, COLOR_GREEN); drawText5x7(200, 138, "NO", COLOR_WHITE);
}
void reset_allSettings() { prefs.begin("sysconfig", false); prefs.clear(); prefs.end(); currentTheme = 1; pinEnabled = false; memset(pinCode, 0, sizeof(pinCode)); applyTheme(1); drawLauncherScreen(); }
void reset_clearWifi() { prefs.begin("wifi", false); prefs.clear(); prefs.end(); wifi_reset(); }
void recovery_touch(int16_t x, int16_t y) {
if (recoveryState == 1) { if (y >= 120 && y <= 165) { if (x >= 40 && x <= 150) { reset_allSettings(); recoveryState = 0; } else if (x >= 170 && x <= 280) { recoveryState = 0; recovery_drawMenu(); } } return; }
if (recoveryState == 2) { if (y >= 120 && y <= 165) { if (x >= 40 && x <= 150) { reset_clearWifi(); recoveryState = 0; } else if (x >= 170 && x <= 280) { recoveryState = 0; recovery_drawMenu(); } } return; }
if (y >= 70 && y <= 105) { recoveryState = 1; recovery_drawConfirm("ALL"); return; }
if (y >= 115 && y <= 150) { recoveryState = 2; recovery_drawConfirm("WIFI"); return; }
if (y >= 160 && y <= 195) { recoveryMode = false; drawLauncherScreen(); return; }
}
void drawWifiIconStatic(int cx, int cy) {
tft_fillCircle(cx, cy, 4, COLOR_GREEN);
int radii[3] = {12, 20, 28};
int thickness = 4;
for (int i = 0; i < 3; i++) {
int r = radii[i];
int rInner = r - thickness;
for (int dy = -r; dy < -2; dy++) {
for (int dx = -r; dx <= r; dx++) {
int distSq = dx * dx + dy * dy;
if (distSq > r * r || distSq < rInner * rInner) continue;
if (abs(dx) > -dy * 12 / 10) continue;
drawPixel(cx + dx, cy + dy, COLOR_GREEN);
}
}
}
}
const int BR_SLIDER_X = 10, BR_SLIDER_Y = 215, BR_SLIDER_W = 300, BR_SLIDER_H = 18;
void drawBrightnessSlider() {
fillRoundRect(BR_SLIDER_X, BR_SLIDER_Y, BR_SLIDER_W, BR_SLIDER_H, cornerMode?3:0, themeTile);
int fillW = BR_SLIDER_W * brightness / 255;
if (fillW > 0) fillRect(BR_SLIDER_X, BR_SLIDER_Y, fillW, BR_SLIDER_H, COLOR_YELLOW);
int knobX = BR_SLIDER_X + fillW; if (knobX > BR_SLIDER_X + BR_SLIDER_W - 4) knobX = BR_SLIDER_X + BR_SLIDER_W - 4;
fillRect(knobX, BR_SLIDER_Y, 4, BR_SLIDER_H, COLOR_WHITE);
drawText5x7(BR_SLIDER_X + BR_SLIDER_W - 40, BR_SLIDER_Y + 5, "BRIGHT", COLOR_WHITE);
}
bool isBrightnessSliderTouched(int16_t x, int16_t y) { return (x >= BR_SLIDER_X - 5 && x <= BR_SLIDER_X + BR_SLIDER_W + 5 && y >= BR_SLIDER_Y - 8 && y <= BR_SLIDER_Y + BR_SLIDER_H + 8); }
void brightness_touch(int16_t x, int16_t y) {
int bx = x - BR_SLIDER_X; if (bx < 0) bx = 0; if (bx > BR_SLIDER_W) bx = BR_SLIDER_W;
int newBrightness = bx * 255 / BR_SLIDER_W; if (newBrightness < 12) newBrightness = 12;
if (newBrightness != brightness) { brightness = newBrightness; applyBrightness(); drawBrightnessSlider(); brightnessDirty = true; }
}
uint16_t lightenColor(uint16_t color, int amount) {
int r = (color >> 11) & 0x1F, g = (color >> 5) & 0x3F, b = color & 0x1F;
r += amount; if (r > 31) r = 31; g += amount * 2; if (g > 63) g = 63; b += amount; if (b > 31) b = 31;
return (r << 11) | (g << 5) | b;
}
void drawGlossyTileCustom(int x, int y, int w, int h) {
int rad = cornerMode ? 3 : 0;
if (aeroMode) {
uint16_t base = themeTile;
fillRoundRect(x, y, w, h, rad, base);
fillRect(x+2, y+2, w-4, h/3, lightenColor(base, 20));
drawLine(x, y, x+w-1, y, COLOR_WHITE);
drawLine(x, y, x, y+h-1, COLOR_WHITE);
drawLine(x, y+h-1, x+w-1, y+h-1, 0x39C7);
drawLine(x+w-1, y, x+w-1, y+h-1, 0x39C7);
return;
}
if (currentTheme == 8) {
uint16_t glass = 0x1148; fillRoundRect(x, y, w, h, rad, glass); fillRect(x + 2, y + 2, w - 4, h / 3, lightenColor(glass, 16));
drawLine(x, y, x + w - 1, y, COLOR_WHITE); drawLine(x, y, x, y + h - 1, COLOR_WHITE);
drawLine(x, y + h - 1, x + w - 1, y + h - 1, 0x39C7); drawLine(x + w - 1, y, x + w - 1, y + h - 1, 0x39C7); return;
}
fillRoundRect(x, y, w, h, rad, themeTile);
drawLine(x, y, x + w, y, COLOR_BLUE); drawLine(x, y + h, x + w, y + h, COLOR_BLUE);
drawLine(x, y, x, y + h, COLOR_BLUE); drawLine(x + w, y, x + w, y + h, COLOR_BLUE);
}
void drawLauncherScreen() {
fillScreenTheme();
int startX = 5, startY = 40, tileW = 60, tileH = 50, gapX = 3, gapY = 5;
auto drawTile = [&](int col, int row, uint16_t iconColor, const char* label, int iconType) {
int x = startX + col * (tileW + gapX), y = startY + row * (tileH + gapY);
drawGlossyTileCustom(x, y, tileW, tileH);
int cx = x + tileW / 2, cy = y + tileH / 2 - 5;
if (iconType == 0) {
drawLine(cx-8, cy-8, cx+8, cy-8, themeText); drawLine(cx-8, cy, cx+8, cy, themeText); drawLine(cx-8, cy+8, cx+8, cy+8, themeText);
drawLine(cx-8, cy-8, cx-8, cy+8, themeText); drawLine(cx, cy-8, cx, cy+8, themeText); drawLine(cx+8, cy-8, cx+8, cy+8, themeText);
drawLine(cx-3, cy-3, cx+3, cy+3, COLOR_RED); drawLine(cx+3, cy-3, cx-3, cy+3, COLOR_RED);
} else if (iconType == 1) {
fillRect(cx-10, cy-10, 12, 12, COLOR_RED); fillRect(cx-2, cy-2, 12, 12, COLOR_BLUE);
fillRect(cx-7, cy-7, 6, 6, COLOR_WHITE); fillRect(cx+1, cy+1, 6, 6, COLOR_WHITE);
} else if (iconType == 2) {
fillRect(cx-8, cy-8, 16, 16, COLOR_BLACK);
fillRect(cx-1, cy-12, 2, 4, COLOR_BLACK); fillRect(cx-1, cy+8, 2, 4, COLOR_BLACK);
fillRect(cx-12, cy-1, 4, 2, COLOR_BLACK); fillRect(cx+8, cy-1, 4, 2, COLOR_BLACK);
fillRect(cx-4, cy-4, 3, 3, COLOR_WHITE);
} else if (iconType == 3) {
fillRect(cx-10, cy-12, 20, 24, COLOR_WHITE); fillRect(cx-10, cy-12, 20, 4, COLOR_BLUE);
drawLine(cx-6, cy-4, cx+6, cy-4, COLOR_GRAY); drawLine(cx-6, cy, cx+6, cy, COLOR_GRAY);
drawLine(cx-6, cy+4, cx+6, cy+4, COLOR_GRAY); drawLine(cx-6, cy+8, cx+2, cy+8, COLOR_GRAY);
} else if (iconType == 4) {
fillRect(cx-12, cy-10, 24, 20, COLOR_CYAN);
fillRect(cx-8, cy-6, 6, 6, COLOR_PINK); fillRect(cx+2, cy-6, 6, 6, COLOR_YELLOW); fillRect(cx-3, cy+2, 6, 6, COLOR_GREEN);
drawLine(cx+8, cy+8, cx+12, cy+12, COLOR_BROWN);
} else if (iconType == 5) { drawWifiIconStatic(cx, cy); }
else if (iconType == 6) {
fillRect(cx-6, cy-6, 12, 12, COLOR_GRAY); fillRect(cx-2, cy-2, 4, 4, themeBg);
fillRect(cx-2, cy-10, 4, 4, COLOR_GRAY); fillRect(cx-2, cy+6, 4, 4, COLOR_GRAY);
fillRect(cx-10, cy-2, 4, 4, COLOR_GRAY); fillRect(cx+6, cy-2, 4, 4, COLOR_GRAY);
} else if (iconType == 7) {
fillRect(cx-10, cy-10, 20, 20, COLOR_YELLOW); fillRect(cx-8, cy-8, 16, 16, COLOR_WHITE);
fillRect(cx-1, cy-6, 2, 8, COLOR_BLACK); fillRect(cx, cy-1, 6, 2, COLOR_BLACK); fillRect(cx-1, cy-1, 2, 2, COLOR_RED);
} else if (iconType == 8) {
fillRect(cx-12, cy-10, 24, 20, COLOR_BLACK);
drawLine(cx-8, cy-4, cx-4, cy, COLOR_GREEN); drawLine(cx-8, cy+4, cx-4, cy, COLOR_GREEN); drawLine(cx-2, cy+4, cx+4, cy+4, COLOR_GREEN);
} else if (iconType == 9) {
fillRect(cx-10, cy-10, 20, 22, COLOR_WHITE); fillRect(cx-10, cy-10, 20, 6, COLOR_RED);
fillRect(cx-6, cy-12, 2, 4, COLOR_GRAY); fillRect(cx+4, cy-12, 2, 4, COLOR_GRAY);
fillRect(cx-6, cy, 2, 2, COLOR_BLACK); fillRect(cx-2, cy, 2, 2, COLOR_BLACK); fillRect(cx+2, cy, 2, 2, COLOR_BLACK);
fillRect(cx-6, cy+4, 2, 2, COLOR_BLACK); fillRect(cx-2, cy+4, 2, 2, COLOR_BLACK); fillRect(cx+2, cy+4, 2, 2, COLOR_BLACK);
fillRect(cx-6, cy+8, 2, 2, COLOR_BLACK); fillRect(cx-2, cy+8, 2, 2, COLOR_BLACK);
} else if (iconType == 10) {
fillRect(cx-10, cy-12, 20, 24, COLOR_PURPLE); fillRect(cx-8, cy-10, 16, 6, COLOR_WHITE);
fillRect(cx-8, cy-2, 4, 4, COLOR_GRAY); fillRect(cx-2, cy-2, 4, 4, COLOR_GRAY); fillRect(cx+4, cy-2, 4, 4, COLOR_GRAY);
fillRect(cx-8, cy+4, 4, 4, COLOR_GRAY); fillRect(cx-2, cy+4, 4, 4, COLOR_GRAY); fillRect(cx+4, cy+4, 4, 4, COLOR_ORANGE);
} else if (iconType == 11) {
fillRect(cx-10, cy-8, 20, 16, COLOR_ORANGE); fillRect(cx-6, cy-4, 12, 8, COLOR_BLACK);
fillRect(cx-4, cy-2, 2, 4, COLOR_YELLOW); fillRect(cx+2, cy-2, 2, 4, COLOR_YELLOW);
} else if (iconType == 12) {
fillRect(cx-10, cy-8, 20, 16, COLOR_LIGHT_ORANGE); fillRect(cx-6, cy-4, 12, 8, COLOR_BLACK);
fillRect(cx-4, cy-2, 2, 4, COLOR_YELLOW); fillRect(cx+2, cy-2, 2, 4, COLOR_YELLOW);
fillRect(cx+6, cy-12, 6, 6, COLOR_RED); drawText5x7(cx+7, cy-11, "2", COLOR_WHITE);
} else if (iconType == 13) {
fillRect(cx-10, cy-10, 20, 20, COLOR_PINK);
fillRect(cx-8, cy+4, 4, 6, COLOR_WHITE); fillRect(cx-2, cy-2, 4, 12, COLOR_WHITE); fillRect(cx+4, cy-8, 4, 18, COLOR_WHITE);
} else if (iconType == 14) {
fillRect(cx-10, cy-10, 20, 20, COLOR_RED);
fillRect(cx-1, cy-8, 2, 8, COLOR_WHITE); fillRect(cx-6, cy-4, 2, 8, COLOR_WHITE);
fillRect(cx+4, cy-4, 2, 8, COLOR_WHITE); fillRect(cx-6, cy+4, 12, 2, COLOR_WHITE);
}
drawText5x7(x + 4, y + tileH - 14, label, themeText);
};
drawTile(0, 0, COLOR_RED, "TicTac", 0); drawTile(1, 0, COLOR_BLUE, "Check", 1); drawTile(2, 0, COLOR_BLACK, "Mines", 2); drawTile(3, 0, COLOR_WHITE, "Note", 3); drawTile(4, 0, COLOR_CYAN, "Scratch", 4);
drawTile(0, 1, COLOR_GREEN, "WiFi", 5); drawTile(1, 1, COLOR_GRAY, "Settings", 6); drawTile(2, 1, COLOR_YELLOW, "Clock", 7); drawTile(3, 1, COLOR_BLACK, "Term", 8); drawTile(4, 1, COLOR_WHITE, "Cal", 9);
drawTile(0, 2, COLOR_PURPLE, "Calc", 10); drawTile(1, 2, COLOR_ORANGE, "Serial1", 11); drawTile(2, 2, COLOR_LIGHT_ORANGE, "Serial2", 12); drawTile(3, 2, COLOR_PINK, "WinLog", 13); drawTile(4, 2, COLOR_RED, "Power", 14);
drawHomeButton(); drawMoreButton(); drawBrightnessSlider();
}
bool winlog_fsReady = false;
void winlog_initFS() { if (winlog_fsReady) return; winlog_fsReady = LittleFS.begin(true); }
void logWin(const char *msg) { winlog_initFS(); if (!winlog_fsReady) return; File f = LittleFS.open("/wins.txt", FILE_APPEND); if (!f) return; char line[96]; snprintf(line, sizeof(line), "[%lus] %s\n", millis() / 1000, msg); f.print(line); f.close(); }
void winlog_clear() { winlog_initFS(); if (!winlog_fsReady) return; LittleFS.remove("/wins.txt"); }
int wl_scroll = 0;
#define WL_MAX_LINES 200
String wl_lines[WL_MAX_LINES]; int wl_lineCount = 0;
void wl_loadLines() { wl_lineCount = 0; winlog_initFS(); if (!winlog_fsReady) return; File f = LittleFS.open("/wins.txt", FILE_READ); if (!f) return; while (f.available() && wl_lineCount < WL_MAX_LINES) { String line = f.readStringUntil('\n'); line.trim(); if (line.length() > 0) wl_lines[wl_lineCount++] = line; } f.close(); }
void drawScrollbar(int x, int yTop, int height, int totalItems, int visibleItems, int scrollOffset);
int scrollbarArrowHit(int x, int yTop, int height, int16_t tx, int16_t ty);
void wl_draw() {
fillScreenTheme(); fillRect(0, 0, 320, 40, themeAccent); drawText5x7(45, 15, "WIN HISTORY", COLOR_WHITE); drawHomeButton(); drawMoreButton();
const int visible = 7;
if (wl_lineCount == 0) { drawText5x7(16, 60, "NO WINS LOGGED YET", themeText); }
else { int y = 45; for (int i = 0; i < visible; i++) { int idx = wl_scroll + i; if (idx >= wl_lineCount) break; fillRect(10, y, 275, 20, themeTile); char buf[52]; wl_lines[idx].toCharArray(buf, 52); drawText5x7(14, y + 6, buf, themeText); y += 22; } }
drawScrollbar(285, 45, 154, wl_lineCount, visible, wl_scroll);
fillRoundRect(10, 205, 145, 28, cornerMode?3:0, COLOR_RED); drawText5x7(45, 213, "CLEAR", COLOR_WHITE);
fillRoundRect(165, 205, 145, 28, cornerMode?3:0, COLOR_GRAY); drawText5x7(190, 213, "BACK", COLOR_WHITE);
}
void wl_reset() { wl_scroll = 0; wl_loadLines(); wl_draw(); }
void wl_touch(int16_t x, int16_t y) {
if (isHomeTouched(x, y)) { globalState = 0; return; }
int scrollHit = scrollbarArrowHit(285, 45, 154, x, y);
if (scrollHit == 0) { if (wl_scroll > 0) wl_scroll--; wl_draw(); return; }
if (scrollHit == 1) { int maxScroll = wl_lineCount > 7 ? wl_lineCount - 7 : 0; if (wl_scroll < maxScroll) wl_scroll++; wl_draw(); return; }
if (y >= 205 && y <= 233) { if (x < 160) { winlog_clear(); wl_reset(); } else { globalState = 7; } }
}
char ttt_board[3][3]; bool ttt_gameOver = false; char ttt_turn = 'O'; bool ttt_ai = true;
const int TTT_X = 60, TTT_Y = 20, TTT_CELL = 60, TTT_AI_BTN_X = 250, TTT_AI_BTN_Y = 90, TTT_AI_BTN_W = 60, TTT_AI_BTN_H = 50;
unsigned long ttt_lastAiTap = 0; int ttt_aiTapCount = 0;
extern bool chk_selected; extern uint8_t chk_turn;
bool ttt_checkWin(char p) { for (int i = 0; i < 3; i++) { if (ttt_board[i][0] == p && ttt_board[i][1] == p && ttt_board[i][2] == p) return true; if (ttt_board[0][i] == p && ttt_board[1][i] == p && ttt_board[2][i] == p) return true; } if (ttt_board[0][0] == p && ttt_board[1][1] == p && ttt_board[2][2] == p) return true; if (ttt_board[0][2] == p && ttt_board[1][1] == p && ttt_board[2][0] == p) return true; return false; }
bool ttt_checkDraw() { for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) if (ttt_board[r][c] == ' ') return false; return true; }
void ttt_drawPiece(int r, int c, char piece) { int cx = TTT_X + c * TTT_CELL + TTT_CELL / 2, cy = TTT_Y + r * TTT_CELL + TTT_CELL / 2; if (piece == 'X') { drawLine(cx - 18, cy - 18, cx + 18, cy + 18, COLOR_RED); drawLine(cx + 18, cy - 18, cx - 18, cy + 18, COLOR_RED); } else { fillRect(cx-10, cy-10, 20, 20, COLOR_BLUE); fillRect(cx-6, cy-6, 12, 12, themeBg); } }
void ttt_status(uint16_t color) { fillRect(0, 0, 40, 240, color); fillRect(280, 0, 40, 240, color); drawHomeButton(); drawMoreButton(); }
void ttt_drawAiButton() { fillRoundRect(TTT_AI_BTN_X, TTT_AI_BTN_Y, TTT_AI_BTN_W, TTT_AI_BTN_H, cornerMode?3:0, ttt_ai ? COLOR_GREEN : COLOR_GRAY); drawText5x7(TTT_AI_BTN_X + 8, TTT_AI_BTN_Y + 10, "AI", COLOR_BLACK); drawText5x7(TTT_AI_BTN_X + 5, TTT_AI_BTN_Y + 28, ttt_ai ? "ON" : "OFF", COLOR_BLACK); }
bool ttt_isAiButtonTouched(int16_t x, int16_t y) { return (x >= TTT_AI_BTN_X && x <= TTT_AI_BTN_X + TTT_AI_BTN_W && y >= TTT_AI_BTN_Y && y <= TTT_AI_BTN_Y + TTT_AI_BTN_H); }
void ttt_aiMove() { if (ttt_gameOver) return; for (int p = 0; p < 2; p++) { char target = (p == 0) ? 'X' : 'O'; for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) if (ttt_board[r][c] == ' ') { ttt_board[r][c] = target; if (ttt_checkWin(target)) { ttt_board[r][c] = 'X'; ttt_drawPiece(r, c, 'X'); return; } ttt_board[r][c] = ' '; } } for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) if (ttt_board[r][c] == ' ') { ttt_board[r][c] = 'X'; ttt_drawPiece(r, c, 'X'); return; } }
void ttt_reset() { for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) ttt_board[r][c] = ' '; ttt_gameOver = false; fillScreenTheme(); for (int i = 1; i < 3; i++) { drawLine(TTT_X + i * TTT_CELL, TTT_Y, TTT_X + i * TTT_CELL, TTT_Y + 3 * TTT_CELL, COLOR_WHITE); drawLine(TTT_X, TTT_Y + i * TTT_CELL, TTT_X + 3 * TTT_CELL, TTT_Y + i * TTT_CELL, COLOR_WHITE); } drawHomeButton(); chk_selected = false; chk_turn = 1; drawMoreButton(); ttt_drawAiButton(); ttt_turn = 'O'; if (ttt_ai) { delay(300); ttt_aiMove(); } }
void ttt_touch(int16_t x, int16_t y) { if (isHomeTouched(x, y)) { globalState = 0; return; } if (ttt_isAiButtonTouched(x, y)) { unsigned long now = millis(); if (now - ttt_lastAiTap < 1200) ttt_aiTapCount++; else ttt_aiTapCount = 1; ttt_lastAiTap = now; if (ttt_aiTapCount >= 3) { ttt_aiTapCount = 0; globalState = 8; fireworks_reset(); return; } ttt_ai = !ttt_ai; ttt_reset(); return; } if (ttt_gameOver) { ttt_reset(); delay(200); return; } if (x < TTT_X || x >= TTT_X + 3 * TTT_CELL || y < TTT_Y || y >= TTT_Y + 3 * TTT_CELL) return; int c = (x - TTT_X) / TTT_CELL, r = (y - TTT_Y) / TTT_CELL; if (ttt_board[r][c] != ' ') return; if (ttt_ai) { ttt_board[r][c] = 'O'; ttt_drawPiece(r, c, 'O'); if (ttt_checkWin('O')) { ttt_status(COLOR_BLUE); ttt_gameOver = true; logWin("TicTacToe: You beat the AI"); return; } if (ttt_checkDraw()) { ttt_status(COLOR_GRAY); ttt_gameOver = true; return; } delay(250); ttt_aiMove(); if (ttt_checkWin('X')) { ttt_status(COLOR_RED); ttt_gameOver = true; logWin("TicTacToe: AI won"); return; } if (ttt_checkDraw()) { ttt_status(COLOR_GRAY); ttt_gameOver = true; return; } } else { ttt_board[r][c] = ttt_turn; ttt_drawPiece(r, c, ttt_turn); if (ttt_checkWin(ttt_turn)) { ttt_status(ttt_turn == 'O' ? COLOR_BLUE : COLOR_RED); ttt_gameOver = true; char msg[40]; snprintf(msg, sizeof(msg), "TicTacToe: Player %c won", ttt_turn); logWin(msg); } else if (ttt_checkDraw()) { ttt_status(COLOR_GRAY); ttt_gameOver = true; } else { ttt_turn = (ttt_turn == 'O') ? 'X' : 'O'; } } }
uint8_t chk_board[8][8]; bool chk_selected = false; int chk_selR = -1, chk_selC = -1; uint8_t chk_turn = 1; bool chk_ai = true;
const int CHK_X = 40, CHK_Y = 10, CHK_CELL = 25, CHK_AI_BTN_X = 250, CHK_AI_BTN_Y = 90, CHK_AI_BTN_W = 60, CHK_AI_BTN_H = 50;
struct ChkMove { int r, c, nr, nc; };
void chk_drawAiButton() { fillRoundRect(CHK_AI_BTN_X, CHK_AI_BTN_Y, CHK_AI_BTN_W, CHK_AI_BTN_H, cornerMode?3:0, chk_ai ? COLOR_GREEN : COLOR_GRAY); drawText5x7(CHK_AI_BTN_X + 8, CHK_AI_BTN_Y + 10, "AI", COLOR_BLACK); drawText5x7(CHK_AI_BTN_X + 5, CHK_AI_BTN_Y + 28, chk_ai ? "ON" : "OFF", COLOR_BLACK); }
bool chk_isAiButtonTouched(int16_t x, int16_t y) { return (x >= CHK_AI_BTN_X && x <= CHK_AI_BTN_X + CHK_AI_BTN_W && y >= CHK_AI_BTN_Y && y <= CHK_AI_BTN_Y + CHK_AI_BTN_H); }
uint8_t chk_checkWinner() { bool redLeft = false, blueLeft = false; for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) { if (chk_board[r][c] == 1) redLeft = true; if (chk_board[r][c] == 2) blueLeft = true; } if (!blueLeft) return 1; if (!redLeft) return 2; return 0; }
void chk_render() { for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) { uint16_t color = ((r + c) % 2 == 0) ? themeBg : themeTile; if (chk_selected && r == chk_selR && c == chk_selC) color = COLOR_GREEN; fillRect(CHK_X + c * CHK_CELL, CHK_Y + r * CHK_CELL, CHK_CELL, CHK_CELL, color); int cx = CHK_X + c * CHK_CELL + CHK_CELL / 2, cy = CHK_Y + r * CHK_CELL + CHK_CELL / 2; if (chk_board[r][c] == 1) fillRect(cx-6, cy-6, 12, 12, COLOR_RED); if (chk_board[r][c] == 2) fillRect(cx-6, cy-6, 12, 12, COLOR_BLUE); } drawHomeButton(); drawMoreButton(); chk_drawAiButton(); }
void chk_reset() { memset(chk_board, 0, sizeof(chk_board)); for (int r = 0; r < 3; r++) for (int c = 0; c < 8; c++) if ((r + c) % 2 == 1) chk_board[r][c] = 2; for (int r = 5; r < 8; r++) for (int c = 0; c < 8; c++) if ((r + c) % 2 == 1) chk_board[r][c] = 1; fillScreenTheme(); chk_render(); }
void chk_aiMove() { ChkMove jumps[32]; int jumpCount = 0; ChkMove moves[64]; int moveCount = 0; for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) { if (chk_board[r][c] != 2) continue; for (int dc = -2; dc <= 2; dc += 4) { int nr = r + 2, nc = c + dc; if (nr < 0 || nr >= 8 || nc < 0 || nc >= 8) continue; if (chk_board[nr][nc] != 0) continue; int midR = r + 1, midC = c + dc / 2; if (chk_board[midR][midC] == 1) { if (jumpCount < 32) jumps[jumpCount++] = {r, c, nr, nc}; } } int nr = r + 1; for (int dc = -1; dc <= 1; dc += 2) { int nc = c + dc; if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8 && chk_board[nr][nc] == 0) { if (moveCount < 64) moves[moveCount++] = {r, c, nr, nc}; } } } if (jumpCount > 0) { int pick = random(0, jumpCount); int midR = (jumps[pick].r + jumps[pick].nr) / 2, midC = (jumps[pick].c + jumps[pick].nc) / 2; chk_board[jumps[pick].nr][jumps[pick].nc] = 2; chk_board[jumps[pick].r][jumps[pick].c] = 0; chk_board[midR][midC] = 0; return; } if (moveCount == 0) return; int pick = random(0, moveCount); chk_board[moves[pick].nr][moves[pick].nc] = 2; chk_board[moves[pick].r][moves[pick].c] = 0; }
void chk_touch(int16_t x, int16_t y) { if (isHomeTouched(x, y)) { globalState = 0; return; } if (chk_isAiButtonTouched(x, y)) { chk_ai = !chk_ai; chk_reset(); return; } if (x < CHK_X || x >= CHK_X + 8 * CHK_CELL || y < CHK_Y || y >= CHK_Y + 8 * CHK_CELL) return; int c = (x - CHK_X) / CHK_CELL, r = (y - CHK_Y) / CHK_CELL; if (!chk_selected) { if (chk_board[r][c] == chk_turn) { chk_selected = true; chk_selR = r; chk_selC = c; chk_render(); } return; } if (r == chk_selR && c == chk_selC) { chk_selected = false; chk_render(); return; } int dr = r - chk_selR, dc = c - chk_selC; bool validMove = false, isJump = false; int capR = -1, capC = -1; if (chk_turn == 1 && dr == -1 && abs(dc) == 1 && chk_board[r][c] == 0) validMove = true; if (chk_turn == 2 && dr == 1 && abs(dc) == 1 && chk_board[r][c] == 0) validMove = true; if (!validMove) { int forwardDir = (chk_turn == 1) ? -1 : 1; if (dr == forwardDir * 2 && abs(dc) == 2 && chk_board[r][c] == 0) { int midR = chk_selR + dr / 2, midC = chk_selC + dc / 2; uint8_t opponent = (chk_turn == 1) ? 2 : 1; if (chk_board[midR][midC] == opponent) { validMove = true; isJump = true; capR = midR; capC = midC; } } } if (validMove) { chk_board[r][c] = chk_turn; chk_board[chk_selR][chk_selC] = 0; if (isJump) chk_board[capR][capC] = 0; chk_selected = false; chk_turn = (chk_turn == 1) ? 2 : 1; chk_render(); if (chk_turn == 2 && chk_ai) { delay(250); chk_aiMove(); chk_turn = 1; chk_render(); } uint8_t winner = chk_checkWinner(); if (winner == 1) logWin("Checkers: You won"); else if (winner == 2) logWin("Checkers: Opponent won"); } }
#define MS_GRID 7
#define MS_CELL 30
#define MS_X 55
#define MS_Y 15
#define MS_MINES 8
uint8_t ms_grid[MS_GRID][MS_GRID]; bool ms_gameOver = false, ms_gameWon = false;
uint16_t ms_numColor(uint8_t n) { switch (n) { case 1: return COLOR_BLUE; case 2: return COLOR_GREEN; case 3: return COLOR_RED; case 4: return COLOR_PURPLE; case 5: return COLOR_BROWN; case 6: return COLOR_CYAN; case 7: return COLOR_BLACK; case 8: return COLOR_GRAY; } return COLOR_WHITE; }
void ms_drawCell(int r, int c) {
int x = MS_X + c * MS_CELL, y = MS_Y + r * MS_CELL;
uint8_t val = ms_grid[r][c];
fillRect(x, y, MS_CELL, MS_CELL, themeBg);
drawLine(x, y, x + MS_CELL - 1, y, COLOR_GRAY);
drawLine(x, y, x, y + MS_CELL - 1, COLOR_GRAY);
if (!(val & 0x20)) { fillRoundRect(x + 2, y + 2, MS_CELL - 4, MS_CELL - 4, cornerMode?3:0, themeTile); return; }
if (val & 0x10) { fillRect(x + MS_CELL/2 - 6, y + MS_CELL/2 - 6, 12, 12, COLOR_BLACK); return; }
uint8_t n = val & 0x0F;
if (n > 0) drawChar5x7(x + MS_CELL/2 - 3, y + MS_CELL/2 - 4, '0' + n, ms_numColor(n));
}
void ms_reveal(int r, int c) { if (r < 0 || r >= MS_GRID || c < 0 || c >= MS_GRID) return; if (ms_grid[r][c] & 0x20) return; ms_grid[r][c] |= 0x20; ms_drawCell(r, c); if ((ms_grid[r][c] & 0x0F) == 0 && !(ms_grid[r][c] & 0x10)) { for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++) ms_reveal(r + dr, c + dc); } }
void ms_checkWin() { for (int r = 0; r < MS_GRID; r++) for (int c = 0; c < MS_GRID; c++) if (!(ms_grid[r][c] & 0x10) && !(ms_grid[r][c] & 0x20)) return; ms_gameWon = true; ms_gameOver = true; fillRect(0, 0, 40, 240, COLOR_BLUE); fillRect(280, 0, 40, 240, COLOR_BLUE); drawHomeButton(); drawMoreButton(); logWin("Minesweeper: You won"); }
void ms_reset() { memset(ms_grid, 0, sizeof(ms_grid)); ms_gameOver = false; ms_gameWon = false; fillScreenTheme(); drawHomeButton(); drawMoreButton(); int planted = 0; while (planted < MS_MINES) { int r = random(0, MS_GRID), c = random(0, MS_GRID); if (!(ms_grid[r][c] & 0x10)) { ms_grid[r][c] |= 0x10; planted++; } } for (int r = 0; r < MS_GRID; r++) for (int c = 0; c < MS_GRID; c++) { if (ms_grid[r][c] & 0x10) continue; uint8_t count = 0; for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++) { int nr = r + dr, nc = c + dc; if (nr >= 0 && nr < MS_GRID && nc >= 0 && nc < MS_GRID) if (ms_grid[nr][nc] & 0x10) count++; } ms_grid[r][c] |= count; } for (int r = 0; r < MS_GRID; r++) for (int c = 0; c < MS_GRID; c++) ms_drawCell(r, c); }
void ms_touch(int16_t x, int16_t y) { if (isHomeTouched(x, y)) { globalState = 0; return; } if (ms_gameOver) { ms_reset(); delay(200); return; } if (x < MS_X || x >= MS_X + MS_GRID * MS_CELL || y < MS_Y || y >= MS_Y + MS_GRID * MS_CELL) return; int c = (x - MS_X) / MS_CELL, r = (y - MS_Y) / MS_CELL; if (ms_grid[r][c] & 0x10) { for (int rr = 0; rr < MS_GRID; rr++) for (int cc = 0; cc < MS_GRID; cc++) if (ms_grid[rr][cc] & 0x10) { ms_grid[rr][cc] |= 0x20; ms_drawCell(rr, cc); } ms_gameOver = true; fillRect(0, 0, 40, 240, COLOR_RED); fillRect(280, 0, 40, 240, COLOR_RED); drawHomeButton(); drawMoreButton(); return; } ms_reveal(r, c); ms_checkWin(); }
char np_text[64]; int np_len = 0; bool np_shift = false;
const char *np_keys[4] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
const int NP_KB_X = 5, NP_KB_W = 310, NP_ROW_H = 34, NP_SHIFT_W = 55, NP_SPACE_W = 140;
const int NP_ROW_Y[5] = {45, 83, 121, 159, 197};
void np_drawKeyboard() {
for (int row = 0; row < 4; row++) { int count = strlen(np_keys[row]); int keyW = NP_KB_W / count; int x = NP_KB_X; for (int i = 0; i < count; i++) { char k = np_keys[row][i]; fillRoundRect(x + 1, NP_ROW_Y[row], keyW - 2, NP_ROW_H, cornerMode?3:0, COLOR_GRAY); drawChar5x7(x + keyW / 2 - 2, NP_ROW_Y[row] + NP_ROW_H / 2 - 4, k, COLOR_WHITE); x += keyW; } }
int y = NP_ROW_Y[4]; int shiftX = NP_KB_X, spaceX = shiftX + NP_SHIFT_W + 5, delX = spaceX + NP_SPACE_W + 5, delW = NP_KB_X + NP_KB_W - delX;
fillRoundRect(shiftX, y, NP_SHIFT_W, NP_ROW_H, cornerMode?3:0, np_shift ? COLOR_GREEN : COLOR_GRAY); drawText5x7(shiftX + 2, y + NP_ROW_H / 2 - 4, "SHFT", np_shift ? COLOR_BLACK : COLOR_WHITE);
fillRoundRect(spaceX, y, NP_SPACE_W, NP_ROW_H, cornerMode?3:0, COLOR_GRAY); drawText5x7(spaceX + NP_SPACE_W / 2 - 15, y + NP_ROW_H / 2 - 4, "SPACE", COLOR_WHITE);
fillRoundRect(delX, y, delW, NP_ROW_H, cornerMode?3:0, COLOR_GRAY); drawText5x7(delX + delW / 2 - 9, y + NP_ROW_H / 2 - 4, "DEL", COLOR_WHITE);
}
void np_reset() { np_len = 0; np_shift = false; memset(np_text, 0, sizeof(np_text)); fillScreenTheme(); fillRect(0, 0, 320, 40, themeAccent); drawText5x7(10, 15, np_text, COLOR_WHITE); fillRect(0, 40, 320, 200, themeTile); drawHomeButton(); drawMoreButton(); np_drawKeyboard(); }
void np_drawText() { fillRect(0, 0, 320, 40, themeAccent); drawText5x7(10, 15, np_text, COLOR_WHITE); }
void np_touch(int16_t x, int16_t y) {
if (isHomeTouched(x, y)) { globalState = 0; return; }
for (int row = 0; row < 4; row++) { if (y >= NP_ROW_Y[row] && y < NP_ROW_Y[row] + NP_ROW_H) { int count = strlen(np_keys[row]); int keyW = NP_KB_W / count; int col = (x - NP_KB_X) / keyW; if (col < 0 || col >= count) return; char k = np_keys[row][col]; char toStore = k; if (row > 0 && !np_shift) toStore = k + 32; if (np_len < 63) { np_text[np_len++] = toStore; np_text[np_len] = 0; } np_drawText(); return; } }
int by = NP_ROW_Y[4]; if (y >= by && y < by + NP_ROW_H) { int shiftX = NP_KB_X, spaceX = shiftX + NP_SHIFT_W + 5, delX = spaceX + NP_SPACE_W + 5; if (x >= shiftX && x < shiftX + NP_SHIFT_W) { np_shift = !np_shift; np_drawKeyboard(); } else if (x >= spaceX && x < spaceX + NP_SPACE_W) { if (np_len < 63) { np_text[np_len++] = ' '; np_text[np_len] = 0; } np_drawText(); } else if (x >= delX && x < NP_KB_X + NP_KB_W) { if (np_len > 0) np_len--; np_text[np_len] = 0; np_drawText(); } }
}
uint16_t sp_color = COLOR_WHITE; int sp_brush = 3; bool sp_isDrawing = false; int sp_lastX = -1, sp_lastY = -1;
void sp_drawUI() {
fillRoundRect(0, 200, 40, 40, cornerMode?3:0, COLOR_RED); fillRoundRect(40, 200, 40, 40, cornerMode?3:0, COLOR_GREEN); fillRoundRect(80, 200, 40, 40, cornerMode?3:0, COLOR_BLUE); fillRoundRect(120, 200, 40, 40, cornerMode?3:0, COLOR_WHITE); fillRoundRect(160, 200, 40, 40, cornerMode?3:0, COLOR_BLACK);
fillRoundRect(210, 200, 30, 40, cornerMode?3:0, sp_brush == 2 ? COLOR_YELLOW : COLOR_GRAY); drawText5x7(218, 215, "S", COLOR_WHITE);
fillRoundRect(245, 200, 30, 40, cornerMode?3:0, sp_brush == 4 ? COLOR_YELLOW : COLOR_GRAY); drawText5x7(253, 215, "M", COLOR_WHITE);
fillRoundRect(280, 200, 30, 40, cornerMode?3:0, sp_brush == 8 ? COLOR_YELLOW : COLOR_GRAY); drawText5x7(288, 215, "L", COLOR_WHITE);
drawHomeButton(); drawMoreButton();
}
void sp_reset() { fillScreen(COLOR_BLACK); sp_isDrawing = false; sp_lastX = -1; sp_lastY = -1; sp_color = COLOR_WHITE; sp_brush = 3; sp_drawUI(); }
void sp_touch(int16_t x, int16_t y) {
if (isHomeTouched(x, y)) { globalState = 0; sp_isDrawing = false; sp_lastX = -1; sp_lastY = -1; return; }
if (y >= 200) {
if (x < 40) sp_color = COLOR_RED; else if (x < 80) sp_color = COLOR_GREEN; else if (x < 120) sp_color = COLOR_BLUE; else if (x < 160) sp_color = COLOR_WHITE; else if (x < 200) sp_color = COLOR_BLACK;
else if (x < 240) { sp_brush = 2; sp_drawUI(); }
else if (x < 275) { sp_brush = 4; sp_drawUI(); }
else { sp_brush = 8; sp_drawUI(); }
sp_isDrawing = false; sp_lastX = -1; sp_lastY = -1; return;
}
if (!sp_isDrawing) { sp_isDrawing = true; sp_lastX = x; sp_lastY = y; fillRect(x-sp_brush/2, y-sp_brush/2, sp_brush, sp_brush, sp_color); return; }
int dx = abs(x - sp_lastX), dy = abs(y - sp_lastY), sx = sp_lastX < x ? 1 : -1, sy = sp_lastY < y ? 1 : -1, err = dx - dy;
int cx = sp_lastX, cy = sp_lastY;
while (true) { fillRect(cx-sp_brush/2, cy-sp_brush/2, sp_brush, sp_brush, sp_color); if (cx == x && cy == y) break; int e2 = 2 * err; if (e2 > -dy) { err -= dy; cx += sx; } if (e2 < dx) { err += dx; cy += sy; } }
sp_lastX = x; sp_lastY = y;
}
void drawUpArrow(int cx, int cy) { for (int i = 0; i < 8; i++) { int w = 2 * i + 1; fillRect(cx - i, cy + i, w, 3, COLOR_WHITE); } }
void drawDownArrow(int cx, int cy) { for (int i = 0; i < 8; i++) { int w = 2 * (7 - i) + 1; fillRect(cx - (7 - i), cy + i, w, 3, COLOR_WHITE); } }
void drawScrollbar(int x, int yTop, int height, int totalItems, int visibleItems, int scrollOffset) { const int arrowH = 34; int trackY0 = yTop + arrowH, trackY1 = yTop + height - arrowH, trackH = trackY1 - trackY0; fillRoundRect(x, yTop, 30, arrowH, cornerMode?3:0, COLOR_GRAY); drawUpArrow(x + 15, yTop + 5); fillRoundRect(x, trackY1, 30, arrowH, cornerMode?3:0, COLOR_GRAY); drawDownArrow(x + 15, trackY1 + 5); fillRect(x, trackY0, 30, trackH, themeTile); int thumbH = trackH, thumbY = trackY0; if (totalItems > visibleItems) { thumbH = trackH * visibleItems / totalItems; if (thumbH < 10) thumbH = 10; int maxScroll = totalItems - visibleItems; thumbY = trackY0 + (trackH - thumbH) * scrollOffset / maxScroll; } fillRect(x + 3, thumbY, 24, thumbH, COLOR_BLUE); }
int scrollbarArrowHit(int x, int yTop, int height, int16_t tx, int16_t ty) { if (tx < x || tx > x + 30) return -1; const int arrowH = 34; if (ty >= yTop && ty < yTop + arrowH) return 0; if (ty >= yTop + height - arrowH && ty < yTop + height) return 1; return -1; }
#define WIFI_MAX_NETWORKS 8
#define SAVED_WIFI_MAX 5
String saved_ssid[SAVED_WIFI_MAX], saved_pass[SAVED_WIFI_MAX]; int saved_wifi_count = 0;
void savedWifi_load() { prefs.begin("savedwifi", true); saved_wifi_count = prefs.getInt("count", 0); if (saved_wifi_count > SAVED_WIFI_MAX) saved_wifi_count = SAVED_WIFI_MAX; for (int i = 0; i < saved_wifi_count; i++) { char key[8]; snprintf(key, sizeof(key), "ssid%d", i); saved_ssid[i] = prefs.getString(key, ""); snprintf(key, sizeof(key), "pass%d", i); saved_pass[i] = prefs.getString(key, ""); } prefs.end(); }
void savedWifi_persist() { prefs.begin("savedwifi", false); prefs.putInt("count", saved_wifi_count); for (int i = 0; i < saved_wifi_count; i++) { char key[8]; snprintf(key, sizeof(key), "ssid%d", i); prefs.putString(key, saved_ssid[i]); snprintf(key, sizeof(key), "pass%d", i); prefs.putString(key, saved_pass[i]); } prefs.end(); }
void savedWifi_remember(const String &ssid, const String &pass) { for (int i = 0; i < saved_wifi_count; i++) if (saved_ssid[i] == ssid) { saved_pass[i] = pass; savedWifi_persist(); return; } if (saved_wifi_count < SAVED_WIFI_MAX) { saved_ssid[saved_wifi_count] = ssid; saved_pass[saved_wifi_count] = pass; saved_wifi_count++; } else { for (int i = 0; i < SAVED_WIFI_MAX - 1; i++) { saved_ssid[i] = saved_ssid[i + 1]; saved_pass[i] = saved_pass[i + 1]; } saved_ssid[SAVED_WIFI_MAX - 1] = ssid; saved_pass[SAVED_WIFI_MAX - 1] = pass; } savedWifi_persist(); }
String savedWifi_lookup(const String &ssid) { for (int i = 0; i < saved_wifi_count; i++) if (saved_ssid[i] == ssid) return saved_pass[i]; return ""; }
String wifi_ssids[WIFI_MAX_NETWORKS]; int wifi_count = 0, wifi_selected = -1, wifi_uiState = 0; char wifi_pass[64]; int wifi_passLen = 0; bool wifi_shift = false, wifi_showPass = false, nat_enabled = false;
char ip_base[16], ip_octet[4]; int ip_octetLen = 0; char nat_ssid[33] = "Espressif O.S", nat_pass[64] = "Espressif O.S"; int nat_len = 0; bool nat_editingPass = false;
#define NET_SCAN_MAX 60
IPAddress netScan_ips[NET_SCAN_MAX]; int netScan_count = 0, netScan_scroll = 0;
const char *ip_keys[4] = { "123", "456", "789", " 0<" }; const int IP_KB_X = 5, IP_KB_W = 310, IP_ROW_Y0 = 80, IP_ROW_H = 31, IP_CELL_H = 27;
const char *wifi_keys[4] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
const int WIFI_SHIFT_BTN_X = 5, WIFI_SHIFT_BTN_Y = 174, WIFI_SHIFT_BTN_W = 60, WIFI_SHIFT_BTN_H = 26, WIFI_DEL_BTN_X = 70, WIFI_DEL_BTN_Y = 174, WIFI_DEL_BTN_W = 60, WIFI_DEL_BTN_H = 26, WIFI_CONNECT_BTN_X = 135, WIFI_CONNECT_BTN_Y = 174, WIFI_CONNECT_BTN_W = 110, WIFI_CONNECT_BTN_H = 26, WIFI_SHOW_BTN_X = 250, WIFI_SHOW_BTN_Y = 174, WIFI_SHOW_BTN_W = 65, WIFI_SHOW_BTN_H = 26;
void wifi_drawHeader(const char *title) { fillRect(0, 0, 320, 40, themeAccent); drawText5x7(45, 15, title, COLOR_WHITE); drawHomeButton(); drawMoreButton(); }
void wifi_drawList() { fillScreenTheme(); wifi_drawHeader("WIFI NETWORKS"); if (wifi_count == 0) drawText5x7(16, 60, "NO NETWORKS FOUND", themeText); int y = 45; for (int i = 0; i < wifi_count; i++) { fillRect(10, y, 300, 16, themeTile); char buf[34]; wifi_ssids[i].toCharArray(buf, 34); drawText5x7(16, y + 4, buf, themeText); y += 19; } fillRoundRect(10, 205, 300, 28, cornerMode?3:0, COLOR_GRAY); drawText5x7(120, 213, "RESCAN", COLOR_WHITE); }
void wifi_scan() {
Serial.println("WIFI: entering wifi_scan()");
fillScreenTheme();
wifi_drawHeader("WIFI NETWORKS");
drawText5x7(16, 60, "SCANNING...", themeText);
Serial.println("WIFI: disconnecting any previous association");
WiFi.disconnect();
delay(150);
Serial.println("WIFI: starting async scanNetworks");
WiFi.scanNetworks(true);
int result = WIFI_SCAN_RUNNING;
unsigned long start = millis();
while (result == WIFI_SCAN_RUNNING && millis() - start < 8000) {
delay(50);
result = WiFi.scanComplete();
}
Serial.printf("WIFI: scan finished, result = %d\n", result);
wifi_count = 0;
if (result > 0) {
for (int i = 0; i < result && wifi_count < WIFI_MAX_NETWORKS; i++) {
wifi_ssids[wifi_count++] = WiFi.SSID(i);
}
}
WiFi.scanDelete();
Serial.println("WIFI: drawing list");
wifi_uiState = 0;
wifi_drawList();
Serial.println("WIFI: wifi_scan() done");
}
void wifi_drawPassField() { fillRect(0, 42, 320, 20, themeBg); int n = wifi_passLen > 18 ? 18 : wifi_passLen; if (!wifi_showPass) { char stars[20]; for (int i = 0; i < n; i++) stars[i] = '*'; stars[n] = 0; drawText5x7(10, 45, stars, COLOR_YELLOW); return; } int x = 10; for (int i = 0; i < n; i++) { drawChar5x7(x, 45, wifi_pass[i], themeText); x += 6; } }
void wifi_drawButtonsRow() { fillRoundRect(WIFI_SHIFT_BTN_X, WIFI_SHIFT_BTN_Y, WIFI_SHIFT_BTN_W, WIFI_SHIFT_BTN_H, cornerMode?3:0, wifi_shift ? COLOR_GREEN : COLOR_GRAY); drawText5x7(WIFI_SHIFT_BTN_X + 15, WIFI_SHIFT_BTN_Y + 9, "SHIFT", wifi_shift ? COLOR_BLACK : COLOR_WHITE); fillRoundRect(WIFI_DEL_BTN_X, WIFI_DEL_BTN_Y, WIFI_DEL_BTN_W, WIFI_DEL_BTN_H, cornerMode?3:0, COLOR_GRAY); drawText5x7(WIFI_DEL_BTN_X + 21, WIFI_DEL_BTN_Y + 9, "DEL", COLOR_WHITE); fillRoundRect(WIFI_CONNECT_BTN_X, WIFI_CONNECT_BTN_Y, WIFI_CONNECT_BTN_W, WIFI_CONNECT_BTN_H, cornerMode?3:0, COLOR_GREEN); drawText5x7(WIFI_CONNECT_BTN_X + 34, WIFI_CONNECT_BTN_Y + 9, "CONNECT", COLOR_BLACK); fillRoundRect(WIFI_SHOW_BTN_X, WIFI_SHOW_BTN_Y, WIFI_SHOW_BTN_W, WIFI_SHOW_BTN_H, cornerMode?3:0, COLOR_GRAY); drawText5x7(WIFI_SHOW_BTN_X + 20, WIFI_SHOW_BTN_Y + 9, wifi_showPass ? "HIDE" : "SHOW", COLOR_WHITE); }
void wifi_drawKeyboard() { fillScreenTheme(); char sBuf[34]; if (wifi_selected >= 0) wifi_ssids[wifi_selected].toCharArray(sBuf, 34); else sBuf[0] = 0; char header[48]; snprintf(header, sizeof(header), "PW FOR %s", sBuf); wifi_drawHeader(header); wifi_drawPassField(); int y = 70; for (int row = 0; row < 4; row++) { int count = strlen(wifi_keys[row]); int keyW = 310 / count; int x = 5; for (int i = 0; i < count; i++) { char k = wifi_keys[row][i]; fillRoundRect(x + 1, y, keyW - 2, 26, cornerMode?3:0, COLOR_GRAY); if (k != ' ') drawChar5x7(x + keyW / 2 - 2, y + 9, k, COLOR_WHITE); x += keyW; } y += 26; } wifi_drawButtonsRow(); }
void wifi_drawConnectedResult() { fillScreenTheme(); wifi_drawHeader("CONNECTED"); char ssidBuf[34]; wifi_ssids[wifi_selected].toCharArray(ssidBuf, 34); drawText5x7(16, 55, ssidBuf, themeText); drawText5x7(16, 70, "CONNECTED", COLOR_GREEN); char ipBuf[24]; WiFi.localIP().toString().toCharArray(ipBuf, 24); drawText5x7(16, 85, ipBuf, themeText); fillRoundRect(10, 105, 300, 28, cornerMode?3:0, COLOR_GREEN); drawText5x7(95, 113, "BROWSE IP", COLOR_BLACK); fillRoundRect(10, 140, 300, 28, cornerMode?3:0, COLOR_GRAY); drawText5x7(90, 148, nat_enabled ? "NAT SETTINGS" : "SETUP NAT (ALPHA)", COLOR_WHITE); fillRoundRect(10, 205, 300, 28, cornerMode?3:0, COLOR_GRAY); drawText5x7(110, 213, "BACK TO LIST", COLOR_WHITE); wifi_uiState = 2; }
void wifi_connect() {
Serial.println("WIFI: entering wifi_connect()");
fillScreenTheme();
wifi_drawHeader("CONNECTING");
char ssidBuf[34], passBuf[64];
wifi_ssids[wifi_selected].toCharArray(ssidBuf, 34);
memcpy(passBuf, wifi_pass, wifi_passLen);
passBuf[wifi_passLen] = 0;
drawText5x7(16, 60, ssidBuf, themeText);
WiFi.begin(ssidBuf, passBuf);
int tries = 0;
while (WiFi.status() != WL_CONNECTED && tries < 30) {
delay(300);
tries++;
}
bool connected = (WiFi.status() == WL_CONNECTED);
if (connected) {
savedWifi_remember(ssidBuf, passBuf);
wifi_drawConnectedResult();
} else {
fillRect(0, 80, 320, 60, themeBg);
drawText5x7(16, 90, "FAILED TO CONNECT", COLOR_RED);
fillRoundRect(10, 205, 300, 28, cornerMode?3:0, COLOR_GRAY);
drawText5x7(110, 213, "BACK TO LIST", COLOR_WHITE);
wifi_uiState = 2;
}
}
void wifi_computeBase() { IPAddress ip = WiFi.localIP(); snprintf(ip_base, sizeof(ip_base), "%d.%d.%d.", ip[0], ip[1], ip[2]); }
void wifi_drawIpText() { fillRect(0, 56, 320, 16, themeBg); char full[24]; snprintf(full, sizeof(full), "IP %s%s", ip_base, ip_octet); drawText5x7(10, 58, full, COLOR_YELLOW); }
void wifi_drawIpKeyboard() { fillScreenTheme(); wifi_drawHeader("IP BROWSER"); char subnetLabel[24]; snprintf(subnetLabel, sizeof(subnetLabel), "SUBNET %s", ip_base); drawText5x7(10, 45, subnetLabel, COLOR_GRAY); wifi_drawIpText(); int y = IP_ROW_Y0; for (int row = 0; row < 4; row++) { int count = strlen(ip_keys[row]); int keyW = IP_KB_W / count; int x = IP_KB_X; for (int i = 0; i < count; i++) { char k = ip_keys[row][i]; fillRoundRect(x + 1, y, keyW - 2, IP_CELL_H, cornerMode?3:0, COLOR_GRAY); if (k != ' ') drawChar5x7(x + keyW / 2 - 2, y + IP_CELL_H / 2 - 4, k, COLOR_WHITE); x += keyW; } y += IP_ROW_H; } fillRoundRect(10, 205, 145, 28, cornerMode?3:0, COLOR_GREEN); drawText5x7(45, 213, "VISIT", COLOR_BLACK); fillRoundRect(165, 205, 145, 28, cornerMode?3:0, COLOR_GRAY); drawText5x7(178, 213, "SCAN NET", COLOR_WHITE); }
void wifi_drawNetList() { fillScreenTheme(); wifi_drawHeader("NETWORK DEVICES"); if (netScan_count == 0) drawText5x7(16, 60, "NO DEVICES FOUND", themeText); const int visible = 6; int y = 45; for (int i = 0; i < visible; i++) { int idx = netScan_scroll + i; if (idx >= netScan_count) break; fillRect(10, y, 275, 20, themeTile); char buf[20]; IPAddress ip = netScan_ips[idx]; snprintf(buf, sizeof(buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]); drawText5x7(16, y + 6, buf, themeText); y += 24; } drawScrollbar(285, 45, 144, netScan_count, visible, netScan_scroll); fillRoundRect(10, 205, 145, 28, cornerMode?3:0, COLOR_GRAY); drawText5x7(35, 213, "RESCAN", COLOR_WHITE); fillRoundRect(165, 205, 145, 28, cornerMode?3:0, COLOR_GRAY); drawText5x7(178, 213, "TYPE IP", COLOR_WHITE); }
void wifi_scanNetworkDevices() { netScan_count = 0; netScan_scroll = 0; fillScreenTheme(); wifi_drawHeader("SCANNING NETWORK"); IPAddress mine = WiFi.localIP(); char progressBuf[24]; for (int octet = 1; octet <= 254 && netScan_count < NET_SCAN_MAX; octet++) { IPAddress target(mine[0], mine[1], mine[2], octet); if (target == mine) continue; if (octet % 10 == 1) { fillRect(0, 60, 320, 16, themeBg); snprintf(progressBuf, sizeof(progressBuf), "CHECKING %d / 254", octet); drawText5x7(16, 62, progressBuf, themeText); } WiFiClient client; if (client.connect(target, 80, 150)) { netScan_ips[netScan_count++] = target; client.stop(); } } wifi_uiState = 5; wifi_drawNetList(); }
void wifi_visitIp() { char fullIp[24]; snprintf(fullIp, sizeof(fullIp), "%s%s", ip_base, ip_octet); fillScreenTheme(); wifi_drawHeader("FETCHING..."); drawText5x7(16, 55, fullIp, themeText); HTTPClient http; char urlBuf[48]; snprintf(urlBuf, sizeof(urlBuf), "http://%s/", fullIp); const char *headerKeys[] = { "Server" }; http.collectHeaders(headerKeys, 1); http.begin(urlBuf); http.setTimeout(5000); int code = http.GET(); fillRect(0, 68, 320, 137, themeBg); if (code > 0) { char codeBuf[16]; snprintf(codeBuf, sizeof(codeBuf), "HTTP %d", code); drawText5x7(10, 68, codeBuf, COLOR_GREEN); String body = http.getString(); String title = ""; int tStart = body.indexOf("<title"); if (tStart >= 0) { int gt = body.indexOf('>', tStart); int tEnd = body.indexOf("</title", gt); if (gt >= 0 && tEnd > gt) { title = body.substring(gt + 1, tEnd); title.trim(); } } String server = http.header("Server"); char idBuf[54]; if (title.length() > 0) { char tBuf[40]; title.toCharArray(tBuf, 40); snprintf(idBuf, sizeof(idBuf), "DEVICE: %s", tBuf); } else if (server.length() > 0) { char sBuf[40]; server.toCharArray(sBuf, 40); snprintf(idBuf, sizeof(idBuf), "SERVER: %s", sBuf); } else { snprintf(idBuf, sizeof(idBuf), "DEVICE: UNKNOWN"); } drawText5x7(10, 80, idBuf, COLOR_CYAN); const char *raw = body.c_str(); int rawLen = body.length(); int idx = 0, lineY = 94; char lineBuf[54]; int lineLen = 0; bool inTag = false, inScript = false, inStyle = false; while (idx < rawLen && lineY < 200) { char c = raw[idx++]; if (c == '<') { inTag = true; if (strncasecmp(raw + idx, "script", 6) == 0) inScript = true; else if (strncasecmp(raw + idx, "/script", 7) == 0) inScript = false; else if (strncasecmp(raw + idx, "style", 5) == 0) inStyle = true; else if (strncasecmp(raw + idx, "/style", 6) == 0) inStyle = false; else { char c1 = raw[idx]; char c2 = (idx + 1 < rawLen) ? raw[idx + 1] : 0; bool isBreak = (tolower(c1) == 'b' && tolower(c2) == 'r') || (c1 == '/' && (tolower(c2) == 'p' || tolower(c2) == 'd' || tolower(c2) == 'l' || tolower(c2) == 't' || tolower(c2) == 'h')) || (tolower(c1) == 'p' && (c2 == '>' || c2 == ' ')); if (isBreak && lineLen > 0) { lineBuf[lineLen] = 0; drawText5x7(10, lineY, lineBuf, themeText); lineY += 10; lineLen = 0; } } continue; } if (c == '>') { inTag = false; continue; } if (inTag || inScript || inStyle) continue; if (c == '\r') continue; if (c == '\n') { if (lineLen > 0) { lineBuf[lineLen] = 0; drawText5x7(10, lineY, lineBuf, themeText); lineY += 10; lineLen = 0; } continue; } if (c == '&') { if (strncasecmp(raw + idx, "amp;", 4) == 0) { c = '&'; idx += 4; } else if (strncasecmp(raw + idx, "lt;", 3) == 0) { c = '<'; idx += 3; } else if (strncasecmp(raw + idx, "gt;", 3) == 0) { c = '>'; idx += 3; } else if (strncasecmp(raw + idx, "quot;", 5) == 0) { c = '"'; idx += 5; } else if (strncasecmp(raw + idx, "nbsp;", 5) == 0) { c = ' '; idx += 5; } else if (strncasecmp(raw + idx, "#39;", 4) == 0) { c = '\''; idx += 4; } } if ((c == ' ' || c == '\t') && (lineLen == 0 || lineBuf[lineLen - 1] == ' ')) continue; if (lineLen < 53) lineBuf[lineLen++] = c; if (lineLen >= 53) { lineBuf[lineLen] = 0; drawText5x7(10, lineY, lineBuf, themeText); lineY += 10; lineLen = 0; } } if (lineLen > 0 && lineY < 200) { lineBuf[lineLen] = 0; drawText5x7(10, lineY, lineBuf, themeText); } } else { drawText5x7(10, 68, "REQUEST FAILED", COLOR_RED); } http.end(); fillRoundRect(10, 205, 145, 28, cornerMode?3:0, COLOR_GRAY); drawText5x7(20, 213, "ANOTHER IP", COLOR_WHITE); fillRoundRect(165, 205, 145, 28, cornerMode?3:0, COLOR_GRAY); drawText5x7(185, 213, "WIFI LIST", COLOR_WHITE); wifi_uiState = 4; }
void wifi_drawNatPrompt() { fillScreenTheme(); wifi_drawHeader("ENABLE NAT?"); drawText5x7(16, 65, "SHARE THIS WIFI AS A", themeText); drawText5x7(16, 80, "HOTSPOT WITH NAT?", themeText); fillRoundRect(40, 120, 110, 40, cornerMode?3:0, COLOR_GREEN); drawText5x7(75, 135, "YES", COLOR_BLACK); fillRoundRect(170, 120, 110, 40, cornerMode?3:0, COLOR_RED); drawText5x7(200, 135, "NO", COLOR_WHITE); }
void wifi_drawNatField() { fillRect(0, 42, 320, 20, themeBg); if (!nat_editingPass) { drawText5x7(10, 45, nat_ssid, themeText); } else { int n = nat_len > 18 ? 18 : nat_len; char stars[20]; for (int i = 0; i < n; i++) stars[i] = '*'; stars[n] = 0; drawText5x7(10, 45, stars, COLOR_YELLOW); } }
void wifi_drawNatKeyboard() { fillScreenTheme(); wifi_drawHeader(nat_editingPass ? "HOTSPOT PASSWORD" : "HOTSPOT SSID"); wifi_drawNatField(); int y = 70; for (int row = 0; row < 4; row++) { int count = strlen(wifi_keys[row]); int keyW = 310 / count; int x = 5; for (int i = 0; i < count; i++) { char k = wifi_keys[row][i]; fillRoundRect(x + 1, y, keyW - 2, 26, cornerMode?3:0, COLOR_GRAY); drawChar5x7(x + keyW / 2 - 2, y + 9, k, COLOR_WHITE); x += keyW; } y += 26; } fillRoundRect(5, 174, 60, 26, cornerMode?3:0, COLOR_GRAY); drawText5x7(20, 183, "DEL", COLOR_WHITE); fillRoundRect(70, 174, 110, 26, cornerMode?3:0, COLOR_GREEN); drawText5x7(nat_editingPass ? 85 : 95, 183, nat_editingPass ? "ENABLE" : "NEXT", COLOR_BLACK); fillRoundRect(185, 174, 130, 26, cornerMode?3:0, COLOR_GRAY); drawText5x7(195, 183, "CANCEL", COLOR_WHITE); }
void wifi_startNat() { EEPROM.write(7, 1); EEPROM.commit(); fillScreenTheme(); wifi_drawHeader("STARTING HOTSPOT"); delay(500); drawText5x7(16, 60, nat_ssid, themeText); fillRect(0, 0, 340, 340, COLOR_WHITE); ledcWrite(TFT_BL, 0); delay(500); delay(150); WiFi.softAP(nat_ssid, strlen(nat_pass) > 0 ? nat_pass : NULL); esp_netif_t *apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"); if (apNetif) { esp_netif_ip_info_t apIp; esp_netif_get_ip_info(apNetif, &apIp); ip_napt_enable(apIp.ip.addr, 1); } nat_enabled = true; EEPROM.commit(); fillRect(0, 0, 340, 240, themeBg); drawText5x7(16, 90, "HOTSPOT ACTIVE", COLOR_GREEN); char ipBuf[24]; WiFi.softAPIP().toString().toCharArray(ipBuf, 24); drawText5x7(16, 105, ipBuf, themeText); fillRoundRect(10, 205, 300, 28, cornerMode?3:0, COLOR_GRAY); drawText5x7(110, 213, "BACK TO LIST", COLOR_WHITE); delay(500); applyBrightness(); wifi_uiState = 8; EEPROM.write(7, 0); }
void wifi_reset() { wifi_selected = -1; wifi_passLen = 0; memset(wifi_pass, 0, sizeof(wifi_pass)); wifi_uiState = 0; wifi_scan(); }
void wifi_touch(int16_t x, int16_t y) {
if (isHomeTouched(x, y)) { globalState = 0; return; }
if (wifi_uiState == 0) { if (y >= 205 && y <= 233) { wifi_scan(); return; } if (x >= 10 && x <= 310 && y >= 45) { int idx = (y - 45) / 19; int rowY = 45 + idx * 19; if (idx >= 0 && idx < wifi_count && y >= rowY && y <= rowY + 16) { wifi_selected = idx; wifi_shift = false; wifi_showPass = false; String saved = savedWifi_lookup(wifi_ssids[idx]); wifi_passLen = saved.length(); if (wifi_passLen > 63) wifi_passLen = 63; memset(wifi_pass, 0, sizeof(wifi_pass)); saved.toCharArray(wifi_pass, 64); wifi_uiState = 1; wifi_drawKeyboard(); } } return; }
if (wifi_uiState == 1) { if (y >= WIFI_SHIFT_BTN_Y && y <= WIFI_SHIFT_BTN_Y + WIFI_SHIFT_BTN_H) { if (x >= WIFI_SHIFT_BTN_X && x <= WIFI_SHIFT_BTN_X + WIFI_SHIFT_BTN_W) { wifi_shift = !wifi_shift; wifi_drawButtonsRow(); return; } if (x >= WIFI_DEL_BTN_X && x <= WIFI_DEL_BTN_X + WIFI_DEL_BTN_W) { if (wifi_passLen > 0) wifi_passLen--; wifi_pass[wifi_passLen] = 0; wifi_drawPassField(); return; } if (x >= WIFI_CONNECT_BTN_X && x <= WIFI_CONNECT_BTN_X + WIFI_CONNECT_BTN_W) { if (wifi_selected >= 0) wifi_connect(); return; } if (x >= WIFI_SHOW_BTN_X && x <= WIFI_SHOW_BTN_X + WIFI_SHOW_BTN_W) { wifi_showPass = !wifi_showPass; wifi_drawPassField(); wifi_drawButtonsRow(); return; } return; } if (y >= 70 && y <= 174) { int row = (y - 70) / 26; if (row < 0 || row > 3) return; int count = strlen(wifi_keys[row]); int keyW = 310 / count; int col = (x - 5) / keyW; if (col < 0 || col >= count) return; char k = wifi_keys[row][col]; char toStore = k; if (!wifi_shift) toStore = k + 32; if (wifi_passLen < 63) { wifi_pass[wifi_passLen++] = toStore; wifi_pass[wifi_passLen] = 0; } wifi_drawPassField(); } return; }
if (wifi_uiState == 2) { if (y >= 105 && y <= 133) { wifi_computeBase(); ip_octetLen = 0; memset(ip_octet, 0, sizeof(ip_octet)); wifi_uiState = 3; wifi_drawIpKeyboard(); return; } if (y >= 140 && y <= 168) { wifi_uiState = 6; wifi_drawNatPrompt(); return; } if (y >= 205 && y <= 233) { wifi_scan(); return; } return; }
if (wifi_uiState == 3) { if (y >= 205 && y <= 233) { if (x < 160) { if (ip_octetLen > 0) wifi_visitIp(); } else { wifi_scanNetworkDevices(); } return; } if (y >= IP_ROW_Y0 && y < IP_ROW_Y0 + 4 * IP_ROW_H) { int row = (y - IP_ROW_Y0) / IP_ROW_H; if (row < 0 || row > 3) return; int rowTop = IP_ROW_Y0 + row * IP_ROW_H; if (y >= rowTop + IP_CELL_H) return; int count = strlen(ip_keys[row]); int keyW = IP_KB_W / count; int col = (x - IP_KB_X) / keyW; if (col < 0 || col >= count) return; char k = ip_keys[row][col]; if (k == '<') { if (ip_octetLen > 0) ip_octetLen--; ip_octet[ip_octetLen] = 0; } else if (k != ' ') { if (ip_octetLen < 3) { ip_octet[ip_octetLen++] = k; ip_octet[ip_octetLen] = 0; } } wifi_drawIpText(); } return; }
if (wifi_uiState == 4) { if (y >= 205 && y <= 233) { if (x < 160) { wifi_uiState = 3; wifi_drawIpKeyboard(); } else { wifi_scan(); } } return; }
if (wifi_uiState == 5) { int scrollHit = scrollbarArrowHit(285, 45, 144, x, y); if (scrollHit == 0) { if (netScan_scroll > 0) netScan_scroll--; wifi_drawNetList(); return; } if (scrollHit == 1) { int maxScroll = netScan_count > 6 ? netScan_count - 6 : 0; if (netScan_scroll < maxScroll) netScan_scroll++; wifi_drawNetList(); return; } if (y >= 205 && y <= 233) { if (x < 160) { wifi_scanNetworkDevices(); } else { wifi_uiState = 3; wifi_drawIpKeyboard(); } return; } if (x >= 10 && x <= 285 && y >= 45) { int row = (y - 45) / 24; int rowY = 45 + row * 24; int idx = netScan_scroll + row; if (row >= 0 && idx < netScan_count && y >= rowY && y <= rowY + 20) { IPAddress ip = netScan_ips[idx]; snprintf(ip_base, sizeof(ip_base), "%d.%d.%d.", ip[0], ip[1], ip[2]); snprintf(ip_octet, sizeof(ip_octet), "%d", ip[3]); ip_octetLen = strlen(ip_octet); wifi_visitIp(); } } return; }
if (wifi_uiState == 6) { if (y >= 120 && y <= 160) { if (x >= 40 && x <= 150) { nat_editingPass = false; wifi_uiState = 7; wifi_drawNatKeyboard(); } else if (x >= 170 && x <= 280) { wifi_drawConnectedResult(); } } return; }
if (wifi_uiState == 7) { if (y >= 174 && y <= 200) { if (x >= 5 && x <= 65) { if (!nat_editingPass) { int l = strlen(nat_ssid); if (l > 0) nat_ssid[l - 1] = 0; } else { if (nat_len > 0) { nat_len--; nat_pass[nat_len] = 0; } } wifi_drawNatField(); return; } if (x >= 70 && x <= 180) { if (!nat_editingPass) { nat_editingPass = true; nat_len = strlen(nat_pass); wifi_drawNatKeyboard(); } else { wifi_startNat(); } return; } if (x >= 185 && x <= 315) { wifi_drawConnectedResult(); return; } return; } if (y >= 70 && y <= 174) { int row = (y - 70) / 26; if (row < 0 || row > 3) return; int count = strlen(wifi_keys[row]); int keyW = 310 / count; int col = (x - 5) / keyW; if (col < 0 || col >= count) return; char k = wifi_keys[row][col]; char toStore = (k >= 'A' && k <= 'Z') ? k + 32 : k; if (!nat_editingPass) { int l = strlen(nat_ssid); if (l < 32) { nat_ssid[l] = toStore; nat_ssid[l + 1] = 0; } } else { if (nat_len < 63) { nat_pass[nat_len++] = toStore; nat_pass[nat_len] = 0; } } wifi_drawNatField(); } return; }
if (wifi_uiState == 8) { if (y >= 205 && y <= 233) { wifi_scan(); return; } return; }
}
int temp_uiState = 0, temp_len = 0, temp_minEntered = 0; char temp_buf[4] = "";
void temp_persist() { prefs.begin("sysconfig", false); prefs.putInt("heatLow", heaterLowF); prefs.putInt("heatMax", heaterMaxF); prefs.end(); }
void temp_updateField() { fillRect(0, 42, 320, 18, themeBg); drawText5x7(140, 45, temp_buf, COLOR_YELLOW); }
void temp_drawPad(const char *prompt) { fillScreenTheme(); fillRect(0, 0, 320, 40, themeAccent); drawText5x7(30, 15, prompt, COLOR_WHITE); drawHomeButton(); drawMoreButton(); temp_updateField(); int y = 80; for (int row = 0; row < 4; row++) { int count = strlen(ip_keys[row]); int keyW = 310 / count; int x = 5; for (int i = 0; i < count; i++) { char k = ip_keys[row][i]; fillRoundRect(x + 1, y, keyW - 2, 27, cornerMode?3:0, COLOR_GRAY); if (k != ' ') drawChar5x7(x + keyW / 2 - 2, y + 10, k, COLOR_WHITE); x += keyW; } y += 31; } fillRoundRect(10, 207, 145, 28, cornerMode?3:0, COLOR_GREEN); drawText5x7(60, 215, "SET", COLOR_BLACK); fillRoundRect(165, 207, 145, 28, cornerMode?3:0, COLOR_GRAY); drawText5x7(180, 215, "CANCEL", COLOR_WHITE); }
void temp_reset() { temp_uiState = 0; temp_len = 0; memset(temp_buf, 0, sizeof(temp_buf)); temp_drawPad("SET MIN TEMP F (FLOOR)"); }
void temp_touch(int16_t x, int16_t y) { if (isHomeTouched(x, y)) { globalState = 7; os_reset(); return; } if (y >= 207 && y <= 235) { if (x < 160) { int val = atoi(temp_buf); if (temp_uiState == 0) { if (val < 32) val = 32; temp_minEntered = val; temp_uiState = 1; temp_len = 0; memset(temp_buf, 0, sizeof(temp_buf)); temp_drawPad("SET MAX TEMP F (CEILING)"); } else { int maxVal = val; if (maxVal <= temp_minEntered) maxVal = temp_minEntered + 1; heaterLowF = temp_minEntered; heaterMaxF = maxVal; temp_persist(); globalState = 7; os_reset(); } } else { globalState = 7; os_reset(); } return; } if (y >= 80 && y < 80 + 4 * 31) { int row = (y - 80) / 31; if (row < 0 || row > 3) return; int rowTop = 80 + row * 31; if (y >= rowTop + 27) return; int count = strlen(ip_keys[row]); int keyW = 310 / count; int col = (x - 5) / keyW; if (col < 0 || col >= count) return; char k = ip_keys[row][col]; if (k == '<') { if (temp_len > 0) temp_len--; temp_buf[temp_len] = 0; } else if (k != ' ') { if (temp_len < 3) { temp_buf[temp_len++] = k; temp_buf[temp_len] = 0; } } temp_updateField(); } }
const int CPU_BOX_Y = 95, CPU_BOX_H = 22, CPU_TOGGLE_X = 245, CPU_TOGGLE_W = 55, OVERLAY_TOGGLE_X = 175, OVERLAY_TOGGLE_W = 60;
void os_drawCpuBox() {
fillRect(10, CPU_BOX_Y, 300, CPU_BOX_H, themeTile);
float c = readChipTempC();
float displayTemp = cpu_metric ? c : (c * 9.0f / 5.0f + 32.0f);
uint32_t freq = getCpuFrequencyMhz();
char buf[40]; snprintf(buf, sizeof(buf), "%.1f%s  %luMHZ", displayTemp, cpu_metric ? "C" : "F", (unsigned long)freq);
drawText5x7(16, CPU_BOX_Y + 6, buf, themeText);
fillRoundRect(OVERLAY_TOGGLE_X, CPU_BOX_Y + 2, OVERLAY_TOGGLE_W, CPU_BOX_H - 4, cornerMode?3:0, COLOR_GRAY);
drawText5x7(OVERLAY_TOGGLE_X + 5, CPU_BOX_Y + 6, overlayEnabled ? "OVR:ON" : "OVR:OFF", COLOR_WHITE);
fillRoundRect(CPU_TOGGLE_X, CPU_BOX_Y + 2, CPU_TOGGLE_W, CPU_BOX_H - 4, cornerMode?3:0, COLOR_GRAY);
drawText5x7(CPU_TOGGLE_X + 10, CPU_BOX_Y + 6, cpu_metric ? "->F" : "->C", COLOR_WHITE);
}
bool isCpuToggleTouched(int16_t x, int16_t y) { return (x >= CPU_TOGGLE_X && x <= CPU_TOGGLE_X + CPU_TOGGLE_W && y >= CPU_BOX_Y && y <= CPU_BOX_Y + CPU_BOX_H); }
bool isOverlayToggleTouched(int16_t x, int16_t y) { return (x >= OVERLAY_TOGGLE_X && x <= OVERLAY_TOGGLE_X + OVERLAY_TOGGLE_W && y >= CPU_BOX_Y && y <= CPU_BOX_Y + CPU_BOX_H); }
unsigned long cpuBoxLastUpdate = 0;
int os_uiState = 0, os_themeScroll = 0; char os_pinBuf[5] = "", os_pinFirst[5] = ""; int os_pinLen = 0;
const char *os_themeNames[10] = { "RED THEME", "BLUE THEME", "OLIVE THEME", "SILVER THEME", "LIME THEME", "WINDOWS THEME", "DARK THEME", "LIGHT THEME", "AERO THEME", "CUSTOM" };
const uint16_t os_themeSwatches[10] = { 0x6042, 0x2104, 0x5282, 0x738E, 0x33C3, 0xC618, 0x39C7, 0x8410, 0x1148, 0xF800 };
void os_persistTheme() { prefs.begin("sysconfig", false); prefs.putInt("theme", currentTheme); prefs.end(); }
void os_persistPin() { prefs.begin("sysconfig", false); prefs.putBool("pinEnabled", pinEnabled); prefs.putString("pin", String(pinCode)); prefs.end(); }

void drawToggleSwitch(int x, int y, bool state) {
int width = 40;
int height = 16;
int radius = height / 2;
if (state) {
fillRoundRect(x, y, width, height, radius, COLOR_GREEN);
} else {
fillRoundRect(x, y, width, height, radius, COLOR_RED);
}
int circleX = state ? (x + width - radius - 2) : (x + radius + 2);
int circleY = y + radius;
int circleRadius = radius - 2;
tft_fillCircle(circleX, circleY, circleRadius, COLOR_WHITE);
}

bool isToggleTouched(int x, int y, int toggleX, int toggleY) {
return (x >= toggleX && x <= toggleX + 40 && y >= toggleY && y <= toggleY + 16);
}

void os_drawMain() {
fillScreenTheme(); fillRect(0, 0, 320, 40, themeAccent); drawText5x7(45, 15, "SYSTEM SETTINGS", COLOR_WHITE); drawHomeButton(); drawMoreButton();
fillRoundRect(10, 45, 300, 22, cornerMode?3:0, COLOR_GRAY); drawText5x7(15, 51, "THEMES", COLOR_WHITE);
fillRoundRect(10, 70, 300, 22, cornerMode?3:0, COLOR_GRAY); drawText5x7(15, 76, "HEATER TEMP RANGE", COLOR_WHITE);
os_drawCpuBox();
fillRoundRect(10, 120, 300, 22, cornerMode?3:0, COLOR_GRAY);
char pinLabel[24]; snprintf(pinLabel, sizeof(pinLabel), "PIN LOCK: %s", pinEnabled ? "ON" : "OFF");
drawText5x7(15, 126, pinLabel, COLOR_WHITE);
fillRoundRect(10, 145, 300, 22, cornerMode?3:0, aeroMode ? COLOR_CYAN : COLOR_GRAY);
drawText5x7(15, 151, aeroMode ? "AERO MODE: ON" : "AERO MODE: OFF", COLOR_WHITE);
fillRoundRect(10, 170, 300, 22, cornerMode?3:0, cornerMode ? COLOR_CYAN : COLOR_GRAY);
drawText5x7(15, 176, cornerMode ? "CORNER MODE: ROUNDED" : "CORNER MODE: SHARP", COLOR_WHITE);
fillRoundRect(10, 195, 300, 22, cornerMode?3:0, powerSaverEnabled ? COLOR_CYAN : COLOR_GRAY);
drawText5x7(15, 201, powerSaverEnabled ? "POWER SAVER: ON (50MHZ)" : "POWER SAVER: OFF (240MHZ)", COLOR_WHITE);
fillRoundRect(10, 220, 145, 20, cornerMode?3:0, COLOR_GRAY); drawText5x7(20, 225, "WIN HISTORY", COLOR_WHITE);
fillRoundRect(165, 220, 145, 20, cornerMode?3:0, COLOR_GRAY); drawText5x7(200, 225, "DONE", COLOR_WHITE);

int toggleX = 260;
drawToggleSwitch(toggleX, 148, aeroMode);
drawToggleSwitch(toggleX, 173, cornerMode);
drawToggleSwitch(toggleX, 198, powerSaverEnabled);
}
void os_drawThemes() {
fillScreenTheme(); fillRect(0, 0, 320, 40, themeAccent); drawText5x7(45, 15, "THEMES", COLOR_WHITE); drawHomeButton(); drawMoreButton();
const int visible = 6; int y = 45;
for (int i = 0; i < visible; i++) {
int idx = os_themeScroll + i; if (idx >= 10) break;
fillRoundRect(10, y, 275, 22, cornerMode?3:0, (idx == currentTheme) ? COLOR_GREEN : COLOR_GRAY);
fillRect(16, y + 3, 16, 16, os_themeSwatches[idx]);
drawText5x7(40, y + 7, os_themeNames[idx], (idx == currentTheme) ? COLOR_BLACK : COLOR_WHITE);
y += 26;
}
drawScrollbar(285, 45, 156, 10, visible, os_themeScroll);
fillRoundRect(10, 210, 300, 26, cornerMode?3:0, COLOR_GRAY); drawText5x7(130, 218, "BACK", COLOR_WHITE);
}
int ct_target = 0;
float ct_hue = 0, ct_sat = 1.0, ct_val = 0.5;
bool ct_wheelDragging = false;
bool ct_sliderDragging = false;
uint16_t hsvToRgb565(float h, float s, float v) {
float r, g, b;
int i = (int)(h / 60.0f) % 6;
float f = h / 60.0f - i;
float p = v * (1.0f - s);
float q = v * (1.0f - f * s);
float t = v * (1.0f - (1.0f - f) * s);
switch(i) {
case 0: r=v; g=t; b=p; break;
case 1: r=q; g=v; b=p; break;
case 2: r=p; g=v; b=t; break;
case 3: r=p; g=q; b=v; break;
case 4: r=t; g=p; b=v; break;
default: r=v; g=p; b=q; break;
}
uint8_t r8 = (uint8_t)(r * 255);
uint8_t g8 = (uint8_t)(g * 255);
uint8_t b8 = (uint8_t)(b * 255);
return ((r8 & 0xF8) << 8) | ((g8 & 0xFC) << 3) | (b8 >> 3);
}
void os_drawColorWheel() {
int cx = 80, cy = 115, radius = 65;
for (int py = cy - radius; py <= cy + radius; py++) {
for (int px = cx - radius; px <= cx + radius; px++) {
int dx = px - cx, dy = py - cy;
int distSq = dx * dx + dy * dy;
if (distSq <= radius * radius) {
float dist = sqrt((float)distSq);
float sat = dist / (float)radius;
float angle = atan2((float)dy, (float)dx);
if (angle < 0) angle += 2.0 * PI;
float hue = angle * 180.0 / PI;
drawPixel(px, py, hsvToRgb565(hue, sat, ct_val));
}
}
}
}
void os_drawCustomTheme() {
fillScreen(COLOR_BLACK);
fillRect(0, 0, 320, 36, COLOR_GRAY);
drawText5x7(40, 13, "CUSTOM THEME EDITOR", COLOR_WHITE);
drawHomeButton();
os_drawColorWheel();
float angle = ct_hue * PI / 180.0;
float dist = ct_sat * 65.0;
int mx = 80 + (int)(cos(angle) * dist);
int my = 115 + (int)(sin(angle) * dist);
fillRect(mx - 3, my - 3, 6, 6, COLOR_WHITE);
fillRect(mx - 2, my - 2, 4, 4, COLOR_BLACK);
int sliderY = 190;
drawText5x7(10, sliderY - 10, "BRIGHTNESS", COLOR_WHITE);
fillRect(10, sliderY, 140, 14, COLOR_BLACK);
for (int i = 0; i < 140; i++) {
float v = (float)i / 140.0;
fillRect(10 + i, sliderY, 1, 14, hsvToRgb565(ct_hue, ct_sat, v));
}
int valPos = (int)(ct_val * 140);
fillRect(10 + valPos - 2, sliderY - 2, 4, 18, COLOR_WHITE);
int px = 170, py = 50, pw = 130, ph = 36;
fillRect(px, py, pw, ph, customBg);
drawText5x7(px + 5, py + 5, "BACKGROUND", ct_target == 0 ? COLOR_YELLOW : COLOR_WHITE);
if (ct_target == 0) { drawLine(px, py, px + pw, py, COLOR_YELLOW); drawLine(px, py + ph, px + pw, py + ph, COLOR_YELLOW); }
py += ph + 6;
fillRect(px, py, pw, ph, customTile);
drawText5x7(px + 5, py + 5, "TILE COLOR", ct_target == 1 ? COLOR_YELLOW : COLOR_WHITE);
if (ct_target == 1) { drawLine(px, py, px + pw, py, COLOR_YELLOW); drawLine(px, py + ph, px + pw, py + ph, COLOR_YELLOW); }
py += ph + 6;
fillRect(px, py, pw, ph, customAccent);
drawText5x7(px + 5, py + 5, "BUTTON COLOR", ct_target == 2 ? COLOR_YELLOW : COLOR_WHITE);
if (ct_target == 2) { drawLine(px, py, px + pw, py, COLOR_YELLOW); drawLine(px, py + ph, px + pw, py + ph, COLOR_YELLOW); }
fillRoundRect(10, 212, 145, 24, cornerMode?3:0, COLOR_GREEN);
drawText5x7(40, 219, "SAVE & APPLY", COLOR_BLACK);
fillRoundRect(165, 212, 145, 24, cornerMode?3:0, COLOR_RED);
drawText5x7(225, 219, "BACK", COLOR_WHITE);
}
void ct_loadTarget() {
uint16_t c = (ct_target == 0) ? customBg : (ct_target == 1) ? customTile : customAccent;
uint8_t r8 = ((c >> 11) & 0x1F) * 255 / 31;
uint8_t g8 = ((c >> 5) & 0x3F) * 255 / 63;
uint8_t b8 = (c & 0x1F) * 255 / 31;
float r = r8 / 255.0f, g = g8 / 255.0f, b = b8 / 255.0f;
float maxC = max(r, max(g, b)), minC = min(r, min(g, b));
ct_val = maxC;
if (maxC == 0) { ct_sat = 0; ct_hue = 0; return; }
ct_sat = (maxC - minC) / maxC;
if (ct_sat == 0) { ct_hue = 0; return; }
if (maxC == r) ct_hue = 60.0 * fmod((g - b) / (maxC - minC), 6.0);
else if (maxC == g) ct_hue = 60.0 * ((b - r) / (maxC - minC) + 2.0);
else ct_hue = 60.0 * ((r - g) / (maxC - minC) + 4.0);
if (ct_hue < 0) ct_hue += 360.0;
}
void ct_applyToTarget() {
uint16_t c = hsvToRgb565(ct_hue, ct_sat, ct_val);
if (ct_target == 0) customBg = c;
else if (ct_target == 1) customTile = c;
else customAccent = c;
}
void os_touchCustomTheme(int16_t x, int16_t y) {
if (isHomeTouched(x, y)) { globalState = 0; return; }
if (ct_wheelDragging || ct_sliderDragging) {
bool needsRedraw = false;
if (ct_wheelDragging) {
int dx = x - 80, dy = y - 115;
int distSq = dx * dx + dy * dy;
if (distSq <= 65 * 65 && y >= 50 && y <= 180) {
float dist = sqrt((float)distSq);
ct_sat = dist / 65.0;
float angle = atan2((float)dy, (float)dx);
if (angle < 0) angle += 2.0 * PI;
ct_hue = angle * 180.0 / PI;
ct_applyToTarget();
needsRedraw = true;
}
}
if (ct_sliderDragging) {
if (y >= 188 && y <= 208 && x >= 10 && x <= 150) {
ct_val = (float)(x - 10) / 140.0;
if (ct_val < 0) ct_val = 0;
if (ct_val > 1) ct_val = 1;
ct_applyToTarget();
needsRedraw = true;
}
}
if (needsRedraw) {
os_drawColorWheel();
float angle = ct_hue * PI / 180.0;
float dist = ct_sat * 65.0;
int mx = 80 + (int)(cos(angle) * dist);
int my = 115 + (int)(sin(angle) * dist);
fillRect(mx - 3, my - 3, 6, 6, COLOR_WHITE);
fillRect(mx - 2, my - 2, 4, 4, COLOR_BLACK);
int sliderY = 190;
fillRect(10, sliderY, 140, 14, COLOR_BLACK);
for (int i = 0; i < 140; i++) {
float v = (float)i / 140.0;
fillRect(10 + i, sliderY, 1, 14, hsvToRgb565(ct_hue, ct_sat, v));
}
int valPos = (int)(ct_val * 140);
fillRect(10 + valPos - 2, sliderY - 2, 4, 18, COLOR_WHITE);
int px = 170, py = 50, pw = 130, ph = 36;
fillRect(px, py, pw, ph, customBg);
py += ph + 6;
fillRect(px, py, pw, ph, customTile);
py += ph + 6;
fillRect(px, py, pw, ph, customAccent);
return;
}
}
int dx = x - 80, dy = y - 115;
int distSq = dx * dx + dy * dy;
if (distSq <= 65 * 65 && y >= 50 && y <= 180) {
ct_wheelDragging = true;
float dist = sqrt((float)distSq);
ct_sat = dist / 65.0;
float angle = atan2((float)dy, (float)dx);
if (angle < 0) angle += 2.0 * PI;
ct_hue = angle * 180.0 / PI;
ct_applyToTarget();
os_drawCustomTheme();
return;
}
if (y >= 188 && y <= 208 && x >= 10 && x <= 150) {
ct_sliderDragging = true;
ct_val = (float)(x - 10) / 140.0;
if (ct_val < 0) ct_val = 0;
if (ct_val > 1) ct_val = 1;
ct_applyToTarget();
os_drawCustomTheme();
return;
}
if (!ct_wheelDragging && !ct_sliderDragging) {
if (x >= 170 && x <= 300) {
if (y >= 50 && y <= 86) { ct_target = 0; ct_loadTarget(); os_drawCustomTheme(); return; }
if (y >= 92 && y <= 128) { ct_target = 1; ct_loadTarget(); os_drawCustomTheme(); return; }
if (y >= 134 && y <= 170) { ct_target = 2; ct_loadTarget(); os_drawCustomTheme(); return; }
}
if (y >= 212 && y <= 236) {
if (x < 160) {
applyTheme(9);
prefs.begin("sysconfig", false);
prefs.putInt("theme", 9);
prefs.putUShort("customBg", customBg);
prefs.putUShort("customTile", customTile);
prefs.putUShort("customAccent", customAccent);
prefs.end();
os_uiState = 0;
os_drawMain();
} else {
os_uiState = 0;
os_drawMain();
}
return;
}
}
}
bool s1_state = false;
void serial1_reset() {
fillScreenTheme(); fillRect(0, 0, 320, 40, themeAccent); drawText5x7(80, 15, "SERIAL PORT 1", COLOR_WHITE); drawHomeButton(); drawMoreButton();
fillRoundRect(50, 80, 220, 60, cornerMode?3:0, s1_state ? COLOR_GREEN : COLOR_RED);
drawText5x7(100, 105, "TOGGLE PIN 21 (LED)", COLOR_WHITE);
}
void serial1_touch(int16_t x, int16_t y) {
if (isHomeTouched(x, y)) { globalState = 0; return; }
if (x >= 50 && x <= 270 && y >= 80 && y <= 140) {
s1_state = !s1_state;
ledcWrite(21, s1_state ? 255 : 0);
serial1_reset();
}
}
float rgb_hue = 0, rgb_sat = 1.0, rgb_val = 0.5;
bool rgb_wheelDragging = false;
bool rgb_sliderDragging = false;
void rgb_updatePins() {
uint16_t c = hsvToRgb565(rgb_hue, rgb_sat, rgb_val);
uint8_t r8 = ((c >> 11) & 0x1F) * 255 / 31;
uint8_t g8 = ((c >> 5) & 0x3F) * 255 / 63;
uint8_t b8 = (c & 0x1F) * 255 / 31;
ledcWrite(21, r8);
ledcWrite(22, g8);
ledcWrite(26, b8);
}
void serial2_drawRGB() {
fillScreen(COLOR_BLACK);
fillRect(0, 0, 320, 36, COLOR_GRAY);
drawText5x7(40, 13, "RGB LED CONTROLLER", COLOR_WHITE);
drawHomeButton();
int cx = 160, cy = 115, radius = 65;
for (int py = cy - radius; py <= cy + radius; py++) {
for (int px = cx - radius; px <= cx + radius; px++) {
int dx = px - cx, dy = py - cy;
int distSq = dx * dx + dy * dy;
if (distSq <= radius * radius) {
float dist = sqrt((float)distSq);
float sat = dist / (float)radius;
float angle = atan2((float)dy, (float)dx);
if (angle < 0) angle += 2.0 * PI;
float hue = angle * 180.0 / PI;
drawPixel(px, py, hsvToRgb565(hue, sat, rgb_val));
}
}
}
float angle = rgb_hue * PI / 180.0;
float dist = rgb_sat * 65.0;
int mx = 160 + (int)(cos(angle) * dist);
int my = 115 + (int)(sin(angle) * dist);
fillRect(mx - 3, my - 3, 6, 6, COLOR_WHITE);
fillRect(mx - 2, my - 2, 4, 4, COLOR_BLACK);
int sliderY = 190;
drawText5x7(10, sliderY - 10, "BRIGHTNESS", COLOR_WHITE);
fillRect(10, sliderY, 300, 14, COLOR_BLACK);
for (int i = 0; i < 300; i++) {
float v = (float)i / 300.0;
fillRect(10 + i, sliderY, 1, 14, hsvToRgb565(rgb_hue, rgb_sat, v));
}
int valPos = (int)(rgb_val * 300);
fillRect(10 + valPos - 2, sliderY - 2, 4, 18, COLOR_WHITE);
fillRoundRect(10, 215, 300, 20, cornerMode?3:0, COLOR_RED);
drawText5x7(140, 220, "TURN OFF LED", COLOR_WHITE);
}
void serial2_reset() { serial2_drawRGB(); rgb_updatePins(); }
void serial2_touch(int16_t x, int16_t y) {
if (isHomeTouched(x, y)) { globalState = 0; return; }
if (rgb_wheelDragging || rgb_sliderDragging) {
bool needsRedraw = false;
if (rgb_wheelDragging) {
int dx = x - 160, dy = y - 115;
int distSq = dx * dx + dy * dy;
if (distSq <= 65 * 65 && y >= 50 && y <= 180) {
float dist = sqrt((float)distSq);
rgb_sat = dist / 65.0;
float angle = atan2((float)dy, (float)dx);
if (angle < 0) angle += 2.0 * PI;
rgb_hue = angle * 180.0 / PI;
rgb_updatePins();
needsRedraw = true;
}
}
if (rgb_sliderDragging) {
if (y >= 188 && y <= 208 && x >= 10 && x <= 310) {
rgb_val = (float)(x - 10) / 300.0;
if (rgb_val < 0) rgb_val = 0;
if (rgb_val > 1) rgb_val = 1;
rgb_updatePins();
needsRedraw = true;
}
}
if (needsRedraw) {
int cx = 160, cy = 115, radius = 65;
for (int py = cy - radius; py <= cy + radius; py++) {
for (int px = cx - radius; px <= cx + radius; px++) {
int ddx = px - cx, ddy = py - cy;
int dDistSq = ddx * ddx + ddy * ddy;
if (dDistSq <= radius * radius) {
float dist = sqrt((float)dDistSq);
float sat = dist / (float)radius;
float angle = atan2((float)ddy, (float)ddx);
if (angle < 0) angle += 2.0 * PI;
float hue = angle * 180.0 / PI;
drawPixel(px, py, hsvToRgb565(hue, sat, rgb_val));
}
}
}
float angle = rgb_hue * PI / 180.0;
float dist = rgb_sat * 65.0;
int mx = 160 + (int)(cos(angle) * dist);
int my = 115 + (int)(sin(angle) * dist);
fillRect(mx - 3, my - 3, 6, 6, COLOR_WHITE);
fillRect(mx - 2, my - 2, 4, 4, COLOR_BLACK);
int sliderY = 190;
fillRect(10, sliderY, 300, 14, COLOR_BLACK);
for (int i = 0; i < 300; i++) {
float v = (float)i / 300.0;
fillRect(10 + i, sliderY, 1, 14, hsvToRgb565(rgb_hue, rgb_sat, v));
}
int valPos = (int)(rgb_val * 300);
fillRect(10 + valPos - 2, sliderY - 2, 4, 18, COLOR_WHITE);
return;
}
}
int dx = x - 160, dy = y - 115;
int distSq = dx * dx + dy * dy;
if (distSq <= 65 * 65 && y >= 50 && y <= 180) {
rgb_wheelDragging = true;
float dist = sqrt((float)distSq);
rgb_sat = dist / 65.0;
float angle = atan2((float)dy, (float)dx);
if (angle < 0) angle += 2.0 * PI;
rgb_hue = angle * 180.0 / PI;
rgb_updatePins();
serial2_drawRGB();
return;
}
if (y >= 188 && y <= 208 && x >= 10 && x <= 310) {
rgb_sliderDragging = true;
rgb_val = (float)(x - 10) / 300.0;
if (rgb_val < 0) rgb_val = 0;
if (rgb_val > 1) rgb_val = 1;
rgb_updatePins();
serial2_drawRGB();
return;
}
if (!rgb_wheelDragging && !rgb_sliderDragging) {
if (y >= 215 && y <= 235) {
rgb_val = 0;
rgb_updatePins();
serial2_drawRGB();
return;
}
}
}
void os_updatePinDots() { fillRect(0, 42, 320, 18, themeBg); char dots[5]; for (int i = 0; i < os_pinLen; i++) dots[i] = '*'; dots[os_pinLen] = 0; drawText5x7(140, 45, dots, COLOR_YELLOW); }
void os_drawPinPad(const char *prompt) { fillScreenTheme(); fillRect(0, 0, 320, 40, themeAccent); drawText5x7(45, 15, prompt, COLOR_WHITE); drawHomeButton(); drawMoreButton(); os_updatePinDots(); int y = 80; for (int row = 0; row < 4; row++) { int count = strlen(ip_keys[row]); int keyW = 310 / count; int x = 5; for (int i = 0; i < count; i++) { char k = ip_keys[row][i]; fillRoundRect(x + 1, y, keyW - 2, 27, cornerMode?3:0, COLOR_GRAY); if (k != ' ') drawChar5x7(x + keyW / 2 - 2, y + 10, k, COLOR_WHITE); x += keyW; } y += 31; } }
void os_reset() { os_uiState = 0; os_themeScroll = (currentTheme > 4) ? currentTheme - 4 : 0; os_drawMain(); }
void os_touch(int16_t x, int16_t y) {
if (os_uiState == 0) {
if (y >= 45 && y <= 67) { os_uiState = 1; os_drawThemes(); return; }
if (y >= 70 && y <= 92) { globalState = 11; temp_reset(); return; }
if (isCpuToggleTouched(x, y)) { cpu_metric = !cpu_metric; os_drawCpuBox(); return; }
if (isOverlayToggleTouched(x, y)) { overlayEnabled = !overlayEnabled; prefs.begin("sysconfig", false); prefs.putBool("overlayEnabled", overlayEnabled); prefs.end(); os_drawCpuBox(); return; }
if (y >= 120 && y <= 142) {
os_pinLen = 0; memset(os_pinBuf, 0, sizeof(os_pinBuf));
if (pinEnabled) { os_uiState = 4; os_drawPinPad("ENTER PIN TO REMOVE"); }
else { os_uiState = 2; os_drawPinPad("SET NEW 4-DIGIT PIN"); }
return;
}
int toggleX = 260;
if (isToggleTouched(x, y, toggleX, 148)) {
aeroMode = !aeroMode;
os_drawMain();
return;
}
if (isToggleTouched(x, y, toggleX, 173)) {
cornerMode = !cornerMode;
os_drawMain();
return;
}
if (isToggleTouched(x, y, toggleX, 198)) {
powerSaverEnabled = !powerSaverEnabled;
prefs.begin("sysconfig", false);
prefs.putBool("powerSaver", powerSaverEnabled);
prefs.end();
if (powerSaverEnabled) {
setCpuFrequencyMhz(50);
} else {
setCpuFrequencyMhz(240);
}
os_drawMain();
return;
}
if (y >= 220 && y <= 240) {
if (x < 160) { globalState = 10; wl_reset(); }
else { globalState = 0; drawLauncherScreen(); }
return;
}
return;
}
if (os_uiState == 1) {
int scrollHit = scrollbarArrowHit(285, 45, 156, x, y);
if (scrollHit == 0) { if (os_themeScroll > 0) os_themeScroll--; os_drawThemes(); return; }
if (scrollHit == 1) { if (os_themeScroll < 10 - 6) os_themeScroll++; os_drawThemes(); return; }
if (x >= 10 && x <= 285 && y >= 45 && y < 201) {
int row = (y - 45) / 26; int rowTop = 45 + row * 26; int idx = os_themeScroll + row;
if (row >= 0 && idx < 10 && y <= rowTop + 22) {
if (idx == 9) {
os_uiState = 5;
ct_target = 0;
ct_loadTarget();
os_drawCustomTheme();
} else {
applyTheme(idx);
os_persistTheme();
os_drawThemes();
}
}
return;
}
if (y >= 210 && y <= 236) { os_uiState = 0; os_drawMain(); return; }
return;
}
if (os_uiState == 5) {
os_touchCustomTheme(x, y);
return;
}
if (y >= 80 && y < 80 + 4 * 31) {
int row = (y - 80) / 31; if (row < 0 || row > 3) return;
int rowTop = 80 + row * 31; if (y >= rowTop + 27) return;
int count = strlen(ip_keys[row]); int keyW = 310 / count; int col = (x - 5) / keyW;
if (col < 0 || col >= count) return;
char k = ip_keys[row][col];
if (k == '<') { if (os_pinLen > 0) os_pinLen--; os_pinBuf[os_pinLen] = 0; }
else if (k != ' ') { if (os_pinLen < 4) { os_pinBuf[os_pinLen++] = k; os_pinBuf[os_pinLen] = 0; } }
if (os_uiState == 2) {
os_updatePinDots();
if (os_pinLen == 4) { strcpy(os_pinFirst, os_pinBuf); os_pinLen = 0; memset(os_pinBuf, 0, sizeof(os_pinBuf)); os_uiState = 3; os_drawPinPad("CONFIRM PIN"); }
} else if (os_uiState == 3) {
os_updatePinDots();
if (os_pinLen == 4) {
if (strcmp(os_pinBuf, os_pinFirst) == 0) { pinEnabled = true; strcpy(pinCode, os_pinBuf); os_persistPin(); os_uiState = 0; os_drawMain(); }
else { os_pinLen = 0; memset(os_pinBuf, 0, sizeof(os_pinBuf)); os_uiState = 2; os_drawPinPad("DIDN'T MATCH - RETRY"); }
}
} else if (os_uiState == 4) {
os_updatePinDots();
if (os_pinLen == 4) {
if (strcmp(os_pinBuf, pinCode) == 0) { pinEnabled = false; os_persistPin(); os_uiState = 0; os_drawMain(); }
else { os_pinLen = 0; memset(os_pinBuf, 0, sizeof(os_pinBuf)); os_uiState = 2; os_drawPinPad("WRONG PIN - RETRY"); }
}
}
}
}
void lock_requirePin() { char entered[5] = ""; int enteredLen = 0; bool unlocked = false; fillScreenTheme(); fillRect(0, 0, 320, 40, themeAccent); drawText5x7(45, 15, "ENTER PIN", COLOR_WHITE); int gridY = 80; for (int row = 0; row < 4; row++) { int count = strlen(ip_keys[row]); int keyW = 310 / count; int x = 5; for (int i = 0; i < count; i++) { char k = ip_keys[row][i]; fillRoundRect(x + 1, gridY, keyW - 2, 27, cornerMode?3:0, COLOR_GRAY); if (k != ' ') drawChar5x7(x + keyW / 2 - 2, gridY + 10, k, COLOR_WHITE); x += keyW; } gridY += 31; } while (!unlocked) { char dots[5]; for (int i = 0; i < enteredLen; i++) dots[i] = '*'; dots[enteredLen] = 0; fillRect(0, 42, 320, 18, themeBg); drawText5x7(140, 45, dots, COLOR_YELLOW); int16_t tx, ty; while (!readTouch(tx, ty)) delay(15); while (readTouch(tx, ty)) delay(15); if (ty < 80 || ty >= 80 + 4 * 31) continue; int row = (ty - 80) / 31; if (row < 0 || row > 3) continue; int rowTop = 80 + row * 31; if (ty >= rowTop + 27) continue; int count = strlen(ip_keys[row]); int keyW = 310 / count; int col = (tx - 5) / keyW; if (col < 0 || col >= count) continue; char k = ip_keys[row][col]; if (k == '<') { if (enteredLen > 0) enteredLen--; entered[enteredLen] = 0; } else if (k != ' ') { if (enteredLen < 4) { entered[enteredLen++] = k; entered[enteredLen] = 0; } } if (enteredLen == 4) { if (strcmp(entered, pinCode) == 0) { unlocked = true; } else { fillRect(0, 60, 320, 16, COLOR_RED); drawText5x7(90, 62, "WRONG PIN", COLOR_WHITE); delay(700); fillRect(0, 60, 320, 16, themeBg); enteredLen = 0; entered[0] = 0; } } } }
int more_prevState = 0;
void power_cancel();
void power_persistSystemStatus() { prefs.begin("sysconfig", false); prefs.putBool("hibernating", true); prefs.putInt("hibState", more_prevState); prefs.end(); }
void power_sleep() { fillScreen(COLOR_RED); delay(10); fillScreen(COLOR_BLUE); delay(10); tft_cmd(0x10); ledcWrite(TFT_BL, 0); delay(10); int16_t tx, ty; while (!readTouch(tx, ty)) delay(20); while (readTouch(tx, ty)) delay(20); tft_cmd(0x11); delay(120); applyBrightness(); delay(20); power_cancel(); }
void power_hibernate() { power_persistSystemStatus(); fillScreenTheme(); fillRect(0, 0, 320, 40, themeAccent); drawText5x7(30, 15, "HIBERNATING - POWER OFF", COLOR_WHITE); drawText5x7(30, 60, "SAFE TO DISCONNECT POWER", themeText); drawText5x7(30, 75, "RECONNECT TO RESUME", themeText); delay(600); fillScreen(COLOR_BLACK); tft_cmd(0x10); ledcWrite(TFT_BL, 0); while (true) { delay(60000); } }
void power_restart() { fillScreenTheme(); fillRect(0, 0, 320, 40, themeAccent); drawText5x7(90, 15, "RESTARTING.", COLOR_WHITE); drawText5x7(90, 15, "RESTARTING..", COLOR_WHITE); drawText5x7(90, 15, "RESTARTING...", COLOR_WHITE); delay(100); for (int i = 0; i < 320; i++) { fillRect(0, 200, i, 40, themeAccent); delay(0); } ESP.restart(); }
void power_cancel() { switch (more_prevState) { case 1: globalState = 1; ttt_reset(); break; case 2: globalState = 2; chk_render(); break; case 3: globalState = 3; ms_reset(); break; case 4: globalState = 4; fillRect(0, 40, 320, 200, themeTile); np_drawText(); np_drawKeyboard(); drawHomeButton(); drawMoreButton(); break; case 5: globalState = 5; sp_reset(); break; case 6: globalState = 6; wifi_reset(); break; case 7: globalState = 7; os_reset(); break; case 10: globalState = 10; wl_reset(); break; case 11: globalState = 11; temp_reset(); break; case 12: globalState = 12; clock_draw(); break; case 13: globalState = 13; calc_draw(); break; case 14: globalState = 14; serial1_reset(); break; case 15: globalState = 15; serial2_reset(); break; case 17: globalState = 17; term_reset(); break; case 18: globalState = 18; cal_reset(); break; default: globalState = 0; drawLauncherScreen(); break; } }
void power_drawMenu() { fillScreenTheme(); fillRect(0, 0, 320, 40, themeAccent); drawText5x7(45, 15, "POWER OPTIONS", COLOR_WHITE); drawHomeButton(); drawMoreButton(); fillRoundRect(40, 50, 240, 36, cornerMode?3:0, COLOR_RED); drawText5x7(140, 63, "SLEEP", themeText); fillRoundRect(40, 92, 240, 36, cornerMode?3:0, COLOR_DARK_ORANGE); drawText5x7(115, 105, "HIBERNATE", COLOR_WHITE); fillRoundRect(40, 134, 240, 36, cornerMode?3:0, COLOR_ORANGE); drawText5x7(130, 147, "RESTART", COLOR_WHITE); fillRoundRect(40, 176, 240, 36, cornerMode?3:0, COLOR_LIGHT_ORANGE); drawText5x7(135, 189, "CANCEL", themeText); }
void power_touch(int16_t x, int16_t y) { if (y >= 50 && y <= 86) { power_sleep(); return; } if (y >= 92 && y <= 128) { power_hibernate(); return; } if (y >= 134 && y <= 170) { power_restart(); return; } if (y >= 176 && y <= 212) { power_cancel(); return; } }

#define HEATER_CHECK_MS 2000UL
bool condensationWarned = false;

void heater_update() { 
    unsigned long now = millis(); 
    if (now - heaterLastCheck < HEATER_CHECK_MS) return; 
    heaterLastCheck = now; 
    heaterLastTempF = heater_readTempF(); 
    float targetF = (heaterLowF + heaterMaxF) / 2.0f; 
    if (heaterLastTempF >= heaterMaxF) { 
        heaterActive = false; 
    } else if (heaterLastTempF < heaterLowF) { 
        heaterActive = true; 
    } else if (heaterLastTempF >= targetF) { 
        heaterActive = false; 
    } 
}

void heater_burn_core0() { 
    volatile double x = 0; 
    for (int i = 0; i < 4000; i++) { 
        x += sin((double)i) * cos((double)i) * sqrt((double)i + 1.0); 
    } 
}

int fw_frameCount = 0;
void fireworks_clear() { fillScreen(COLOR_BLACK); drawText5x7Scaled(92, 110, "Nothing today!", COLOR_YELLOW, 2); drawHomeButton(); }
void fireworks_frame() { fw_frameCount++; if (fw_frameCount % 80 == 1) { fireworks_clear(); } int cx = random(20, 300), cy = random(20, 200); uint16_t colors[6] = { COLOR_RED, COLOR_YELLOW, COLOR_GREEN, COLOR_CYAN, COLOR_PINK, COLOR_PURPLE }; uint16_t c = colors[random(0, 6)]; const int sparks = 10; for (int i = 0; i < sparks; i++) { float angle = (2.0 * PI * i) / sparks; int r = random(8, 22); int px = cx + (int)(cos(angle) * r), py = cy + (int)(sin(angle) * r); drawPixel(px, py, c); } fillRect(cx-2, cy-2, 4, 4, c); drawHomeButton(); }
void fireworks_reset() { fw_frameCount = 0; fireworks_clear(); }
void clock_draw() {
fillScreenTheme();
fillRect(0, 0, 320, 40, themeAccent);
drawText5x7(70, 15, "CLOCK & CALENDAR", COLOR_WHITE);
drawHomeButton();
drawMoreButton();
struct tm timeinfo;
bool timeSynced = getLocalTime(&timeinfo);
if (!timeSynced) {
drawText5x7Scaled(50, 60, "--:--:--", COLOR_YELLOW, 3);
drawText5x7(50, 110, "Waiting for NTP...", themeText);
drawText5x7(80, 150, "No holidays today", themeText);
} else {
char dateBuf[64], timeBuf[32];
strftime(dateBuf, sizeof(dateBuf), "%A, %B %d, %Y", &timeinfo);
strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &timeinfo);
drawText5x7Scaled(50, 60, timeBuf, COLOR_YELLOW, 3);
drawText5x7(50, 110, dateBuf, themeText);
const char* holiday = NULL;
if (timeinfo.tm_mon == 0 && timeinfo.tm_mday == 1) holiday = "New Year's Day";
else if (timeinfo.tm_mon == 6 && timeinfo.tm_mday == 4) holiday = "Independence Day";
else if (timeinfo.tm_mon == 11 && timeinfo.tm_mday == 25) holiday = "Christmas Day";
if (holiday) {
fillRoundRect(40, 140, 240, 30, cornerMode?3:0, COLOR_RED);
drawText5x7(60, 148, holiday, COLOR_WHITE);
} else {
drawText5x7(80, 150, "No holidays today", themeText);
}
}
}
void clock_reset() { clock_draw(); }
void clock_touch(int16_t x, int16_t y) { if (isHomeTouched(x, y)) { globalState = 0; return; } }
void cal_reset() {
fillScreenTheme(); fillRect(0, 0, 320, 40, themeAccent); drawText5x7(90, 15, "CALENDAR", COLOR_WHITE); drawHomeButton(); drawMoreButton();
drawText5x7(40, 60, "July 2026", COLOR_YELLOW);
drawText5x7(20, 90, "Su Mo Tu We Th Fr Sa", themeText);
int x = 20, y = 110;
for(int i=0; i<4; i++) x += 20;
for(int d=1; d<=31; d++) {
char dayStr[4]; snprintf(dayStr, sizeof(dayStr), "%2d", d);
if (d == 16) { fillRect(x - 2, y - 2, 18, 18, COLOR_RED); drawText5x7(x, y, dayStr, COLOR_WHITE); }
else { drawText5x7(x, y, dayStr, themeText); }
x += 20;
if ((d + 4) % 7 == 0) { x = 20; y += 20; }
}
}
void cal_touch(int16_t x, int16_t y) { if (isHomeTouched(x, y)) { globalState = 0; return; } }
String term_cmd = "";
void term_drawKB() {
int y = 160;
const char *keys[3] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
for(int r=0; r<3; r++) {
int count = strlen(keys[r]); int keyW = 310 / count; int x = 5;
for(int i=0; i<count; i++) {
fillRoundRect(x+1, y, keyW-2, 20, cornerMode?3:0, COLOR_GRAY);
drawChar5x7(x+keyW/2-2, y+6, keys[r][i], COLOR_WHITE);
x += keyW;
}
y += 22;
}
fillRoundRect(5, y, 100, 20, cornerMode?3:0, COLOR_GRAY); drawText5x7(40, y+6, "SPACE", COLOR_WHITE);
fillRoundRect(110, y, 100, 20, cornerMode?3:0, COLOR_GREEN); drawText5x7(140, y+6, "ENTER", COLOR_BLACK);
fillRoundRect(215, y, 100, 20, cornerMode?3:0, COLOR_RED); drawText5x7(250, y+6, "DEL", COLOR_WHITE);
}
void term_reset() {
fillScreenTheme(); fillRect(0, 0, 320, 40, themeAccent); drawText5x7(110, 15, "TERMINAL", COLOR_WHITE); drawHomeButton(); drawMoreButton();
fillRect(10, 50, 300, 100, COLOR_BLACK);
drawText5x7(15, 60, "ESP32 Terminal v1.0", COLOR_GREEN);
drawText5x7(15, 75, "Type commands:", COLOR_GREEN);
drawText5x7(15, 90, "> _", COLOR_GREEN);
term_cmd = "";
term_drawKB();
}
void term_execute() {
fillRect(10, 50, 300, 100, COLOR_BLACK);
drawText5x7(15, 60, ("> " + term_cmd).c_str(), COLOR_GREEN);
if (term_cmd == "help") drawText5x7(15, 80, "Commands: help, clear, date, reboot", COLOR_GREEN);
else if (term_cmd == "clear") term_reset();
else if (term_cmd == "date") { struct tm t; if(getLocalTime(&t)) { char b[32]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &t); drawText5x7(15, 80, b, COLOR_GREEN); } else drawText5x7(15, 80, "Time not synced.", COLOR_RED); }
else if (term_cmd == "reboot") ESP.restart();
else drawText5x7(15, 80, "Command not found.", COLOR_RED);
term_cmd = "";
drawText5x7(15, 110, "> _", COLOR_GREEN);
}
void term_touch(int16_t x, int16_t y) {
if (isHomeTouched(x, y)) { globalState = 0; return; }
if (y >= 160) {
int row = (y - 160) / 22;
if (row < 3) {
const char *keys[3] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
int count = strlen(keys[row]); int keyW = 310 / count; int col = (x - 5) / keyW;
if (col >= 0 && col < count) { term_cmd += (char)(keys[row][col] + 32); drawText5x7(15, 110, ("> " + term_cmd + "_").c_str(), COLOR_GREEN); }
} else {
if (x < 105) { term_cmd += " "; drawText5x7(15, 110, ("> " + term_cmd + "_").c_str(), COLOR_GREEN); }
else if (x < 210) term_execute();
else { if(term_cmd.length() > 0) term_cmd.remove(term_cmd.length()-1); drawText5x7(15, 110, ("> " + term_cmd + "_").c_str(), COLOR_GREEN); }
}
}
}
String calc_display = "0"; double calc_val1 = 0, calc_val2 = 0; char calc_op = ' '; bool calc_new_entry = true;
void calc_draw() { fillScreenTheme(); fillRect(0, 0, 320, 40, themeAccent); drawText5x7(100, 15, "CALCULATOR", COLOR_WHITE); drawHomeButton(); drawMoreButton(); fillRect(10, 50, 300, 40, themeTile); drawText5x7(20, 65, calc_display.c_str(), COLOR_YELLOW); const char* keys[4][4] = { {"7", "8", "9", "/"}, {"4", "5", "6", "*"}, {"1", "2", "3", "-"}, {"C", "0", "=", "+"} }; int startX = 10, startY = 100, keyW = 72, keyH = 30, gap = 4; for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) { int x = startX + c * (keyW + gap), y = startY + r * (keyH + gap); uint16_t color = COLOR_GRAY; if (strcmp(keys[r][c], "=") == 0) color = COLOR_GREEN; else if (strcmp(keys[r][c], "C") == 0) color = COLOR_RED; else if (strcmp(keys[r][c], "/") == 0 || strcmp(keys[r][c], "*") == 0 || strcmp(keys[r][c], "-") == 0 || strcmp(keys[r][c], "+") == 0) color = COLOR_LIGHT_ORANGE; fillRoundRect(x, y, keyW, keyH, cornerMode?3:0, color); drawText5x7(x + keyW/2 - 3, y + keyH/2 - 4, keys[r][c], COLOR_WHITE); } }
void calc_reset() { calc_display = "0"; calc_val1 = 0; calc_val2 = 0; calc_op = ' '; calc_new_entry = true; calc_draw(); }
void calc_touch(int16_t x, int16_t y) { if (isHomeTouched(x, y)) { globalState = 0; return; } int startX = 10, startY = 100, keyW = 72, keyH = 30, gap = 4; if (y >= startY && y < startY + 4 * (keyH + gap)) { int r = (y - startY) / (keyH + gap), c = (x - startX) / (keyW + gap); if (r >= 0 && r < 4 && c >= 0 && c < 4) { const char* keys[4][4] = { {"7", "8", "9", "/"}, {"4", "5", "6", "*"}, {"1", "2", "3", "-"}, {"C", "0", "=", "+"} }; String key = keys[r][c]; if (key == "C") { calc_reset(); } else if (key == "=") { if (calc_op != ' ') { calc_val2 = calc_display.toDouble(); double res = 0; if (calc_op == '+') res = calc_val1 + calc_val2; else if (calc_op == '-') res = calc_val1 - calc_val2; else if (calc_op == '*') res = calc_val1 * calc_val2; else if (calc_op == '/') res = calc_val2 != 0 ? calc_val1 / calc_val2 : 0; calc_display = String(res, 4); calc_display.trim(); calc_op = ' '; calc_new_entry = true; } } else if (key == "+" || key == "-" || key == "*" || key == "/") { calc_val1 = calc_display.toDouble(); calc_op = key[0]; calc_new_entry = true; } else { if (calc_new_entry) { calc_display = key; calc_new_entry = false; } else { if (calc_display == "0") calc_display = key; else calc_display += key; } } calc_draw(); } } }

void cpuTask(void* pvParameters) {
    for(;;) {
        unsigned long now = millis();
        if (now - heaterLastCheck >= HEATER_CHECK_MS) {
            heaterLastCheck = now;
            heaterLastTempF = heater_readTempF();
            float targetF = (heaterLowF + heaterMaxF) / 2.0f;
            if (heaterLastTempF >= heaterMaxF) heaterActive = false;
            else if (heaterLastTempF < heaterLowF) heaterActive = true;
            else if (heaterLastTempF >= targetF) heaterActive = false;
        }

        if (heaterActive) {
            heater_burn_core0();
            cpuDisplay = 100;
        } else {
            cpuDisplay = 0;
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }
}

void setup() {
Serial.begin(19200); delay(200); Serial.println("BOOT: starting up");
WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
esp_err_t nvsErr = nvs_flash_init();
if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) { Serial.println("BOOT: NVS corrupt/outdated, erasing and reinitializing"); nvs_flash_erase(); nvsErr = nvs_flash_init(); }
Serial.printf("BOOT: nvs_flash_init result = %d\n", nvsErr);
Serial.println("BOOT: powering up WiFi radio (AP_STA) before display init");
WiFi.mode(WIFI_AP_STA); WiFi.persistent(false); WiFi.setTxPower(WIFI_POWER_2dBm);
configTime(0, 0, "pool.ntp.org", "time.nist.gov");
delay(200);
Serial.println("BOOT: WiFi radio powered up");
Wire.begin(I2C_SDA, I2C_SCL); pinMode(TP_RST, OUTPUT); digitalWrite(TP_RST, HIGH);
ili9341_init(); drawSplashScreen(); delay(1800);
ledcAttach(21, 5000, 8);
ledcAttach(22, 5000, 8);
ledcAttach(26, 5000, 8);
prefs.begin("sysconfig", true);
applyTheme(prefs.getInt("theme", 1)); pinEnabled = prefs.getBool("pinEnabled", false);
String savedPin = prefs.getString("pin", ""); savedPin.toCharArray(pinCode, 5);
bool wasHibernating = prefs.getBool("hibernating", false); int hibState = prefs.getInt("hibState", 0);
brightness = prefs.getInt("brightness", 255); heaterLowF = prefs.getInt("heatLow", 99); heaterMaxF = prefs.getInt("heatMax", 105);
overlayEnabled = prefs.getBool("overlayEnabled", true);
overlayX = 80;
overlayY = 0;
customBg = prefs.getUShort("customBg", 0x0000);
customTile = prefs.getUShort("customTile", 0x4208);
customAccent = prefs.getUShort("customAccent", 0x001F);
cornerMode = prefs.getBool("cornerMode", true);
aeroMode = prefs.getBool("aeroMode", false);
powerSaverEnabled = prefs.getBool("powerSaver", false);
prefs.end(); applyBrightness(); savedWifi_load(); winlog_initFS();
if (powerSaverEnabled) {
setCpuFrequencyMhz(50);
} else {
setCpuFrequencyMhz(240);
}
if (wasHibernating) { prefs.begin("sysconfig", false); prefs.putBool("hibernating", false); prefs.end(); }
if (pinEnabled) lock_requirePin();

xTaskCreatePinnedToCore(cpuTask, "cpuTask", 4096, NULL, 1, &cpuTaskHandle, 0);

if (wasHibernating) {
more_prevState = hibState; power_cancel(); fillRect(0, 220, 320, 20, themeAccent); drawText5x7(65, 226, "RESUMING SYSTEM", COLOR_WHITE); delay(1800); power_cancel();
} else {
drawLauncherScreen();
}
}

void loop() {
unsigned long loopStart = micros();
updateFpsCpu();

float currentChipTempC = readChipTempC();
if (currentChipTempC > 80.0f) {
EEPROM.begin(512);
EEPROM.write(7, 6);
EEPROM.commit();
fillScreen(COLOR_RED);
drawText5x7Scaled(60, 100, "OVERHEAT!", COLOR_WHITE, 2);
delay(1000);
ledcWrite(TFT_BL, 0);
tft_cmd(0x10);
esp_deep_sleep_start();
}

int16_t x, y;
if (readTouch(x, y)) {
if (overlayDragging) { overlayX = x - overlayDragStartX; overlayY = y - overlayDragStartY; if (overlayX < 0) overlayX = 0; if (overlayX > 160) overlayX = 160; if (overlayY < 0) overlayY = 0; if (overlayY > 208) overlayY = 208; drawFpsOverlay(); global_wasTouched = true; }
else if (overlayEnabled && x >= overlayX && x <= overlayX + 160 && y >= overlayY && y <= overlayY + 32) { overlayDragging = true; overlayDragStartX = x - overlayX; overlayDragStartY = y - overlayY; global_wasTouched = true; }
else {
int prev = globalState;
if (isHomeTouched(x, y)) { if (homePressStart == 0) homePressStart = millis(); if (!homeHoldTriggered && millis() - homePressStart > 900) { homeHoldTriggered = true; globalState = 7; os_reset(); } else if (!homeHoldTriggered && !global_wasTouched) { globalState = 0; } global_wasTouched = true; }
else if (isMoreTouched(x, y) && globalState != 8 && globalState != 9) { if (!global_wasTouched) { more_prevState = globalState; globalState = 9; power_drawMenu(); } global_wasTouched = true; }
else {
homePressStart = 0; homeHoldTriggered = false;
if (globalState == 5) { sp_touch(x, y); }
else if (globalState == 0 && isBrightnessSliderTouched(x, y)) { brightness_touch(x, y); }
else if (!global_wasTouched) {
if (globalState == 0) {
int startX = 5, startY = 40, tileW = 60, tileH = 50, gapX = 3, gapY = 5;
int col = (x - startX) / (tileW + gapX), row = (y - startY) / (tileH + gapY);
if (col >= 0 && col < 5 && row >= 0 && row < 3) {
int tileIdx = row * 5 + col;
switch(tileIdx) {
case 0: globalState = 1; ttt_reset(); break; case 1: globalState = 2; chk_reset(); break; case 2: globalState = 3; ms_reset(); break; case 3: globalState = 4; np_reset(); np_drawKeyboard(); break; case 4: globalState = 5; sp_reset(); break;
case 5: globalState = 6; wifi_reset(); break; case 6: globalState = 7; os_reset(); break; case 7: globalState = 12; clock_reset(); break; case 8: globalState = 17; term_reset(); break; case 9: globalState = 18; cal_reset(); break;
case 10: globalState = 13; calc_reset(); break; case 11: globalState = 14; serial1_reset(); break; case 12: globalState = 15; serial2_reset(); break; case 13: globalState = 10; wl_reset(); break; case 14: globalState = 9; power_drawMenu(); break;
}
}
}
else if (globalState == 1) ttt_touch(x, y); else if (globalState == 2) chk_touch(x, y); else if (globalState == 3) ms_touch(x, y); else if (globalState == 4) np_touch(x, y); else if (globalState == 6) wifi_touch(x, y);
else if (globalState == 7) os_touch(x, y); else if (globalState == 9) power_touch(x, y); else if (globalState == 10) wl_touch(x, y); else if (globalState == 11) temp_touch(x, y); else if (globalState == 12) clock_touch(x, y);
else if (globalState == 13) calc_touch(x, y); else if (globalState == 14) serial1_touch(x, y); else if (globalState == 15) serial2_touch(x, y); else if (globalState == 17) term_touch(x, y); else if (globalState == 18) cal_touch(x, y);
}
global_wasTouched = true;
}
if (prev != 0 && globalState == 0) drawLauncherScreen();
}
} else {
if (ct_wheelDragging || ct_sliderDragging) {
ct_wheelDragging = false;
ct_sliderDragging = false;
}
if (rgb_wheelDragging || rgb_sliderDragging) {
rgb_wheelDragging = false;
rgb_sliderDragging = false;
}
if (overlayDragging) { overlayDragging = false; prefs.begin("sysconfig", false); prefs.putInt("overlayX", overlayX); prefs.putInt("overlayY", overlayY); prefs.end(); if (globalState == 0) drawLauncherScreen(); else if (globalState == 7) os_drawMain(); }
global_wasTouched = false; homePressStart = 0; homeHoldTriggered = false;
if (brightnessDirty) { prefs.begin("sysconfig", false); prefs.putInt("brightness", brightness); prefs.end(); brightnessDirty = false; }
if (globalState == 5) { sp_isDrawing = false; sp_lastX = -1; sp_lastY = -1; }
}
if (globalState == 8) fireworks_frame();
unsigned long cpuUpdateInterval = powerSaverEnabled ? 5000 : 1000;
if (globalState == 7 && os_uiState == 0 && millis() - cpuBoxLastUpdate > cpuUpdateInterval) {
cpuBoxLastUpdate = millis();
os_drawCpuBox();
}

unsigned long loopEnd = micros();
unsigned long workTime = loopEnd - loopStart;
gpuWorkAccum += workTime;
unsigned long targetTime = powerSaverEnabled ? 200000 : 33333;
if (workTime < targetTime) {
unsigned long sleepUs = targetTime - workTime;
if (sleepUs >= 1000) {
delay(sleepUs / 1000);
}
unsigned long rem = sleepUs % 1000;
if (rem > 10) delayMicroseconds(rem);
}
}
