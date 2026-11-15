#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "code.h"
#include <HTTPClient.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <math.h>
#include "time.h"

TFT_eSPI tft = TFT_eSPI();

// --- ENCODER 1 PINS (Rotate = Volume | Press = Play/Pause in Spotify) ---
#define ENC1_CLK 32
#define ENC1_DT  33
#define ENC1_SW  25

// --- ENCODER 2 PINS (Press = Open Menu / Select | Rotate = Navigate Menu) ---
#define ENC2_CLK 26
#define ENC2_DT  27
#define ENC2_SW  14

// --- CUSTOM COLORS (RGB565) ---
#define THEME_RED     0xF800  // Bright Red for SPOTIFY header, Vinyl center, and Menu
#define VOL_COLOR     0xFEA0  // Warm Gold for Volume and percentages
#define VINYL_GREY    0x2104
#define SUBTLE_GREY   0x8410

// --- APPLICATION STATES (SCREENS) ---
enum AppScreen {
    SCREEN_SPOTIFY = 0,
    SCREEN_PC_STATS = 1,
    SCREEN_CLOCK = 2,
    SCREEN_MENU = 3
};

AppScreen currentScreen = SCREEN_SPOTIFY;
int menuSelection = 0; // 0 = Spotify, 1 = PC Temp & Usage, 2 = Time & Date
const char* menuItems[3] = { "Spotify Player", "Temp & Usage PC", "Time & Date" };

// --- PC STATS VARIABLES (Default placeholder values until PC Serial connects) ---
int pcCpuTemp = 52;
int pcGpuTemp = 61;
int pcCpuUsage = 35;
int pcGpuUsage = 74;
int pcRamUsage = 62;
bool pcStatsNeedRedraw = false;

// --- CLOCK VARIABLES (NTP) ---
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7200;     // GMT+2 (Adjust for your timezone)
const int   daylightOffset_sec = 3600; // Daylight Saving Time offset
String lastClockStr = "";
String lastDateStr = "";

// --- SPOTIFY API VARIABLES ---
const char* host = "api.spotify.com";
const char* token_host = "accounts.spotify.com";
const int httpsPort = 443;
WiFiClientSecure client;
unsigned long lastTokenTime = 0;
const unsigned long tokenExpireTime = 3600000;
String access_token = "";
bool isPlaying = false;
bool hasValidData = false;
int connectionRetries = 0;
const int maxRetries = 3;

unsigned long trackStartTime = 0;
long serverProgress = 0;
long trackDuration = 0;
String currentTrackId = "";
String lastTrackName = "";
String lastArtistName = "";
bool wasPlayingBeforeUpdate = false;
unsigned long lastServerUpdate = 0;

int dotCount = 0;
unsigned long lastDotTime = 0;

bool uiInitialized = false;
int lastProgressWidth = -1;
bool lastPlayStateBar = true;
String lastDisplayedTime = "";

// --- VINYL ANIMATION & TEXT SCROLLING VARIABLES ---
int vinylAngle = 0;
unsigned long lastVinylUpdate = 0;
int scrollState = 0;
int scrollIndex = 0;
unsigned long lastScrollTime = 0;
const int maxTitleChars = 15;

// --- ENCODER & VOLUME VARIABLES ---
int currentVolume = 50; 
int lastDisplayedVolume = -1;
bool volumeNeedsSync = false;
unsigned long lastVolumeChangeTime = 0;

volatile int enc1Steps = 0;
volatile unsigned long lastEnc1Interrupt = 0;

volatile int enc2Steps = 0;
volatile unsigned long lastEnc2Interrupt = 0;

int lastSw1 = HIGH;
int lastSw2 = HIGH;
unsigned long lastBtn1Time = 0;
unsigned long lastBtn2Time = 0;

int pendingAction = 0; // 0 = None, 1 = Play/Pause

// Interrupt Service Routine for Encoder 1 (Volume control)
void IRAM_ATTR isrEnc1() {
    unsigned long now = millis();
    if (now - lastEnc1Interrupt > 4) {
        if (digitalRead(ENC1_DT) == HIGH) enc1Steps++;
        else enc1Steps--;
        lastEnc1Interrupt = now;
    }
}

// Interrupt Service Routine for Encoder 2 (Menu navigation)
void IRAM_ATTR isrEnc2() {
    unsigned long now = millis();
    if (now - lastEnc2Interrupt > 4) {
        if (digitalRead(ENC2_DT) == HIGH) enc2Steps++;
        else enc2Steps--;
        lastEnc2Interrupt = now;
    }
}

// =========================================================================
// ROMANIAN DIACRITICS & SPECIAL CHARACTER SUPPORT
// =========================================================================
String prepareRomanianText(String text) {
    text.replace("ă", "\x01"); text.replace("Ă", "\x02");
    text.replace("â", "\x03"); text.replace("Â", "\x04");
    text.replace("î", "\x05"); text.replace("Î", "\x06");
    text.replace("ș", "\x07"); text.replace("Ș", "\x08");
    text.replace("ş", "\x07"); text.replace("Ş", "\x08");
    text.replace("ț", "\x0E"); text.replace("Ț", "\x0F");
    text.replace("ţ", "\x0E"); text.replace("Ţ", "\x0F");
    text.replace("’", "'"); text.replace("“", "\"");
    text.replace("”", "\""); text.replace("–", "-");
    return text;
}

void drawRomanianChar(int x, int y, char c, uint16_t fg, uint16_t bg) {
    char base = c;
    int accent = 0; // 1 = breve, 2 = circumflex, 3 = comma below
    switch ((uint8_t)c) {
        case 0x01: base = 'a'; accent = 1; break;
        case 0x02: base = 'A'; accent = 1; break;
        case 0x03: base = 'a'; accent = 2; break;
        case 0x04: base = 'A'; accent = 2; break;
        case 0x05: base = 'i'; accent = 2; break;
        case 0x06: base = 'I'; accent = 2; break;
        case 0x07: base = 's'; accent = 3; break;
        case 0x08: base = 'S'; accent = 3; break;
        case 0x0E: base = 't'; accent = 3; break;
        case 0x0F: base = 'T'; accent = 3; break;
    }
    tft.fillRect(x, y - 2, 6, 2, bg);
    tft.fillRect(x, y + 8, 6, 2, bg);
    tft.drawChar(x, y, base, fg, bg, 1);

    if (accent == 1) {
        tft.drawPixel(x + 1, y - 2, fg);
        tft.drawPixel(x + 4, y - 2, fg);
        tft.drawFastHLine(x + 2, y - 1, 2, fg);
    } else if (accent == 2) {
        tft.drawFastHLine(x + 2, y - 2, 2, fg);
        tft.drawPixel(x + 1, y - 1, fg);
        tft.drawPixel(x + 4, y - 1, fg);
    } else if (accent == 3) {
        tft.drawPixel(x + 2, y + 8, fg);
        tft.drawPixel(x + 1, y + 9, fg);
    }
}

void drawRomanianString(String text, int x, int y, uint16_t fg, uint16_t bg, int maxChars) {
    int len = text.length();
    for (int i = 0; i < maxChars; i++) {
        char c = (i < len) ? text[i] : ' ';
        drawRomanianChar(x + i * 6, y, c, fg, bg);
    }
}

// =========================================================================
// GRADIENT (FADE) HELPER FOR PROGRESS AND USAGE BARS
// =========================================================================
uint16_t getGradientColor(int x, int maxW) {
    // Smooth gradient from Red -> Orange -> Yellow
    uint8_t r = 255;
    uint8_t g = map(x, 0, maxW, 15, 220);
    uint8_t b = 0;
    return tft.color565(r, g, b);
}

void drawFadeBar(int x, int y, int w, int h, int percent) {
    if (percent > 100) percent = 100;
    if (percent < 0) percent = 0;
    int innerW = w - 4;
    int fillW = (percent * innerW) / 100;

    tft.drawRoundRect(x, y, w, h, 2, SUBTLE_GREY);
    for (int i = 0; i < innerW; i++) {
        uint16_t col = (i < fillW) ? getGradientColor(i, innerW) : 0x1082;
        tft.drawFastVLine(x + 2 + i, y + 2, h - 4, col);
    }
}

// =========================================================================
// SCREEN 1: MAIN MENU
// =========================================================================
void drawMenuScreen(bool fullRedraw = false) {
    if (fullRedraw) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(THEME_RED, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(42, 6);
        tft.print("SELECT MODE");
        tft.drawFastHLine(0, 18, 160, 0x2104);
    }

    for (int i = 0; i < 3; i++) {
        int itemY = 28 + i * 32;
        bool selected = (i == menuSelection);

        uint16_t bgCol = selected ? THEME_RED : 0x1082;
        uint16_t txtCol = selected ? TFT_WHITE : SUBTLE_GREY;
        uint16_t borderCol = selected ? TFT_WHITE : 0x2104;

        tft.fillRoundRect(12, itemY, 136, 24, 4, bgCol);
        tft.drawRoundRect(12, itemY, 136, 24, 4, borderCol);

        tft.setTextColor(txtCol, bgCol);
        tft.setTextSize(1);
        tft.setCursor(24, itemY + 8);
        tft.printf("%s %s", selected ? ">" : " ", menuItems[i]);
    }
}

// =========================================================================
// SCREEN 2: PC TEMP & USAGE (CPU/GPU Temp + Usage Bars for CPU, GPU, RAM)
// =========================================================================
void drawPCStatsScreen(bool fullRedraw = false) {
    if (fullRedraw) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(THEME_RED, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(6, 5);
        tft.print("PC MONITOR");

        tft.setTextColor(SUBTLE_GREY, TFT_BLACK);
        tft.setCursor(110, 5);
        tft.print("USB/WiFi");
        tft.drawFastHLine(0, 16, 160, 0x2104);

        // Temperature boxes (Top)
        tft.drawRoundRect(6, 21, 70, 27, 3, 0x2104);
        tft.drawRoundRect(84, 21, 70, 27, 3, 0x2104);
    }

    // 1. CPU Temperature (Top Left)
    tft.setTextSize(1);
    tft.setTextColor(SUBTLE_GREY, TFT_BLACK);
    tft.setCursor(12, 24);
    tft.print("CPU:");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(14, 33);
    tft.printf("%2d", pcCpuTemp);
    tft.setTextSize(1);
    tft.print(" C ");

    // 2. GPU Temperature (Top Right)
    tft.setTextSize(1);
    tft.setTextColor(SUBTLE_GREY, TFT_BLACK);
    tft.setCursor(90, 24);
    tft.print("GPU:");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(92, 33);
    tft.printf("%2d", pcGpuTemp);
    tft.setTextSize(1);
    tft.print(" C ");

    // 3. CPU Usage Bar (%)
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(6, 56);
    tft.print("CPU");
    drawFadeBar(32, 54, 94, 12, pcCpuUsage);
    tft.setTextColor(VOL_COLOR, TFT_BLACK);
    tft.setCursor(130, 56);
    tft.printf("%3d%%", pcCpuUsage);

    // 4. GPU Usage Bar (%)
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(6, 80);
    tft.print("GPU");
    drawFadeBar(32, 78, 94, 12, pcGpuUsage);
    tft.setTextColor(VOL_COLOR, TFT_BLACK);
    tft.setCursor(130, 80);
    tft.printf("%3d%%", pcGpuUsage);

    // 5. RAM / Memory Usage Bar (%)
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(6, 104);
    tft.print("RAM");
    drawFadeBar(32, 102, 94, 12, pcRamUsage);
    tft.setTextColor(VOL_COLOR, TFT_BLACK);
    tft.setCursor(130, 104);
    tft.printf("%3d%%", pcRamUsage);
}

// Reads incoming serial data from PC in format: C:55,G:62,U:35,P:80,M:60\n
void readPCSerialData() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        int cIdx = line.indexOf("C:");
        int gIdx = line.indexOf("G:");
        int uIdx = line.indexOf("U:");
        int pIdx = line.indexOf("P:");
        int mIdx = line.indexOf("M:");

        if (cIdx != -1 && gIdx != -1 && uIdx != -1 && pIdx != -1 && mIdx != -1) {
            pcCpuTemp  = line.substring(cIdx + 2, line.indexOf(',', cIdx)).toInt();
            pcGpuTemp  = line.substring(gIdx + 2, line.indexOf(',', gIdx)).toInt();
            pcCpuUsage = line.substring(uIdx + 2, line.indexOf(',', uIdx)).toInt();
            pcGpuUsage = line.substring(pIdx + 2, line.indexOf(',', pIdx)).toInt();
            pcRamUsage = line.substring(mIdx + 2).toInt();
            pcStatsNeedRedraw = true;
        }
    }
}

// =========================================================================
// SCREEN 3: CLOCK & DATE (Synchronized via NTP)
// =========================================================================
void drawClockScreen(bool fullRedraw = false) {
    if (fullRedraw) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(THEME_RED, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(6, 5);
        tft.print("SYSTEM CLOCK");
        tft.drawFastHLine(0, 18, 160, 0x2104);
        lastClockStr = "";
        lastDateStr = "";
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) {
        tft.setTextColor(SUBTLE_GREY, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(35, 60);
        tft.print("Syncing time...");
        return;
    }

    char timeBuf[16];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &timeinfo);
    String nowTime = String(timeBuf);

    char dateBuf[32];
    strftime(dateBuf, sizeof(dateBuf), "%d.%m.%Y", &timeinfo);
    String nowDate = String(dateBuf);

    if (nowTime != lastClockStr) {
        lastClockStr = nowTime;
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextSize(3);
        tft.setCursor(10, 48);
        tft.print(nowTime);
    }

    if (nowDate != lastDateStr) {
        lastDateStr = nowDate;
        tft.setTextColor(VOL_COLOR, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(22, 92);
        tft.print(nowDate);
    }
}

// =========================================================================
// SCREEN 0: SPOTIFY PLAYER (Vinyl Animation, Diacritics, Slide Text, Fade Bar)
// =========================================================================
void drawVinylBase() {
    int cx = 28, cy = 50;
    tft.fillCircle(cx, cy, 22, VINYL_GREY);
    tft.drawCircle(cx, cy, 22, SUBTLE_GREY);
    tft.drawCircle(cx, cy, 18, TFT_BLACK);
    tft.drawCircle(cx, cy, 14, TFT_BLACK);
    tft.drawCircle(cx, cy, 10, TFT_BLACK);
    tft.fillCircle(cx, cy, 7, THEME_RED);
    tft.fillCircle(cx, cy, 2, TFT_BLACK);
}

void updateVinylAnimation() {
    if (!isPlaying) return;
    if (millis() - lastVinylUpdate < 80) return;
    lastVinylUpdate = millis();

    int cx = 28, cy = 50;
    for (int a = 0; a < 2; a++) {
        float radOld = (vinylAngle + a * 180) * 0.0174533;
        for (int r = 9; r <= 20; r++) {
            int px = cx + cos(radOld) * r;
            int py = cy + sin(radOld) * r;
            uint16_t col = (r == 10 || r == 14 || r == 18) ? TFT_BLACK : VINYL_GREY;
            tft.drawPixel(px, py, col);
        }
    }

    float radOldDot = vinylAngle * 0.0174533;
    tft.drawPixel(cx + cos(radOldDot) * 5, cy + sin(radOldDot) * 5, THEME_RED);

    vinylAngle = (vinylAngle + 18) % 360;

    for (int a = 0; a < 2; a++) {
        float radNew = (vinylAngle + a * 180) * 0.0174533;
        for (int r = 9; r <= 20; r++) {
            int px = cx + cos(radNew) * r;
            int py = cy + sin(radNew) * r;
            tft.drawPixel(px, py, SUBTLE_GREY);
        }
    }

    float radNewDot = vinylAngle * 0.0174533;
    tft.drawPixel(cx + cos(radNewDot) * 5, cy + sin(radNewDot) * 5, TFT_BLACK);
}

void resetTitleScroll() {
    scrollState = 0;
    scrollIndex = 0;
    lastScrollTime = millis();

    if (lastTrackName.length() <= maxTitleChars) {
        drawRomanianString(lastTrackName, 60, 32, TFT_WHITE, TFT_BLACK, maxTitleChars);
    } else {
        String truncated = lastTrackName.substring(0, maxTitleChars - 3) + "...";
        drawRomanianString(truncated, 60, 32, TFT_WHITE, TFT_BLACK, maxTitleChars);
    }
}

void updateTitleScroll() {
    if (!hasValidData || lastTrackName.length() <= maxTitleChars) return;
    unsigned long now = millis();

    if (scrollState == 0) {
        if (now - lastScrollTime > 2500) {
            scrollState = 1;
            scrollIndex = 0;
            lastScrollTime = now;
        }
    } else if (scrollState == 1) {
        if (now - lastScrollTime > 180) {
            lastScrollTime = now;
            scrollIndex++;
            int maxIdx = lastTrackName.length() - maxTitleChars;
            if (scrollIndex <= maxIdx) {
                String sub = lastTrackName.substring(scrollIndex, scrollIndex + maxTitleChars);
                drawRomanianString(sub, 60, 32, TFT_WHITE, TFT_BLACK, maxTitleChars);
            } else {
                scrollState = 2;
            }
        }
    } else if (scrollState == 2) {
        if (now - lastScrollTime > 1500) {
            resetTitleScroll();
        }
    }
}

void updateProgressBar(bool forceRedraw = false) {
    if (currentScreen != SCREEN_SPOTIFY || trackDuration <= 0) return;
    
    long currentProgress = 0;
    if (hasValidData) {
        if (!isPlaying) {
            currentProgress = serverProgress;
        } else {
            currentProgress = serverProgress + (millis() - lastServerUpdate);
            if (currentProgress > trackDuration) currentProgress = trackDuration;
        }
    }

    int barX = 10, barY = 104, barW = 140, barH = 14;
    int innerW = barW - 4;
    float ratio = (float)currentProgress / (float)trackDuration;
    int fillW = (int)(ratio * innerW);
    if (fillW > innerW) fillW = innerW;
    if (fillW < 0) fillW = 0;

    if (fillW != lastProgressWidth || isPlaying != lastPlayStateBar || forceRedraw) {
        lastProgressWidth = fillW;
        lastPlayStateBar = isPlaying;

        tft.drawRoundRect(barX, barY, barW, barH, 3, SUBTLE_GREY);
        for (int i = 0; i < innerW; i++) {
            uint16_t col = (i < fillW) ? getGradientColor(i, innerW) : 0x1082;
            tft.drawFastVLine(barX + 2 + i, barY + 2, barH - 4, col);
        }

        if (!isPlaying) {
            int txtX = barX + (barW - 42) / 2;
            tft.fillRect(txtX - 3, barY + 2, 48, barH - 4, TFT_BLACK);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.setTextSize(1);
            tft.setCursor(txtX, barY + 3);
            tft.print("Stopped");
        }
    }
}

void updateTimeDisplay() {
    if (currentScreen != SCREEN_SPOTIFY) return;

    long currentProgress = isPlaying ? (serverProgress + (millis() - lastServerUpdate)) : serverProgress;
    if (currentProgress > trackDuration) currentProgress = trackDuration;
    if (currentProgress < 0) currentProgress = 0;
    
    int durMin = trackDuration / 60000;
    int durSec = (trackDuration % 60000) / 1000;
    int progMin = currentProgress / 60000;
    int progSec = (currentProgress % 60000) / 1000;

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d|%02d:%02d", progMin, progSec, durMin, durSec);
    String timeKey = String(buf);
    
    if (timeKey != lastDisplayedTime) {
        lastDisplayedTime = timeKey;
        tft.setTextColor(SUBTLE_GREY, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(10, 91);
        tft.printf("%02d:%02d", progMin, progSec);
        tft.setCursor(120, 91);
        tft.printf("%02d:%02d", durMin, durSec);
    }
}

void drawVolumeUI() {
    if (currentScreen != SCREEN_SPOTIFY) return;
    if (currentVolume != lastDisplayedVolume) {
        lastDisplayedVolume = currentVolume;
        tft.fillRect(102, 5, 56, 10, TFT_BLACK);
        tft.setTextColor(VOL_COLOR, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(104, 5);
        tft.printf("VOL:%d%%", currentVolume);
    }
}

void initSpotifyUI() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(THEME_RED, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(6, 5);
    tft.print("SPOTIFY");

    lastDisplayedVolume = -1;
    drawVolumeUI();
    tft.drawFastHLine(0, 18, 160, 0x2104);
    drawVinylBase();

    // Redraw current song details if valid data is already cached
    if (hasValidData) {
        resetTitleScroll();
        String dispArtist = lastArtistName;
        if (dispArtist.length() > maxTitleChars) {
            dispArtist = dispArtist.substring(0, maxTitleChars - 3) + "...";
        }
        drawRomanianString(dispArtist, 60, 52, SUBTLE_GREY, TFT_BLACK, maxTitleChars);
        lastDisplayedTime = "";
        updateTimeDisplay();
        updateProgressBar(true);
    }
    uiInitialized = true;
}

void showLoadingScreen(String message) {
    tft.fillScreen(TFT_BLACK);
    if (millis() - lastDotTime > 400) {
        dotCount = (dotCount + 1) % 4;
        lastDotTime = millis();
    }
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    int textWidth = message.length() * 6;
    tft.setCursor((160 - textWidth) / 2, 60);
    tft.print(message);
    
    String dots = "";
    for(int i = 0; i < dotCount; i++) dots += ".";
    tft.setCursor((160 - textWidth) / 2 + textWidth, 60);
    tft.fillRect((160 - textWidth) / 2 + textWidth, 60, 24, 8, TFT_BLACK);
    tft.print(dots);
}

String getAccessToken() {
    client.setTimeout(8000);
    if (!client.connect(token_host, httpsPort)) return "";

    String body = "grant_type=refresh_token&refresh_token=" + String(refresh_token) +
                  "&client_id=" + String(client_id) +
                  "&client_secret=" + String(client_secret);

    client.println("POST /api/token HTTP/1.1");
    client.println("Host: accounts.spotify.com");
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.print("Content-Length: ");
    client.println(body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);

    unsigned long timeout = millis() + 8000;
    while (client.connected() && millis() < timeout) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }
    if (millis() >= timeout) {
        client.stop();
        return "";
    }

    String response = client.readString();
    client.stop();
    int startIndex = response.indexOf("\"access_token\":\"") + 16;
    int endIndex = response.indexOf("\"", startIndex);
    if (startIndex > 15 && endIndex > startIndex) {
        return response.substring(startIndex, endIndex);
    }
    return "";
}

void setSpotifyVolume(int vol) {
    if (WiFi.status() != WL_CONNECTED || access_token.isEmpty()) return;
    client.setTimeout(2500);
    if (!client.connect(host, httpsPort)) return;

    String url = "/v1/me/player/volume?volume_percent=" + String(vol);
    client.printf("PUT %s HTTP/1.1\r\n", url.c_str());
    client.println("Host: api.spotify.com");
    client.print("Authorization: Bearer ");
    client.println(access_token);
    client.println("Content-Length: 0");
    client.println("Connection: close");
    client.println();
    client.stop();
}

void sendSpotifyCommand(const char* endpoint, const char* method = "POST") {
    if (WiFi.status() != WL_CONNECTED || access_token.isEmpty()) return;
    client.setTimeout(2500);
    if (!client.connect(host, httpsPort)) return;

    client.printf("%s %s HTTP/1.1\r\n", method, endpoint);
    client.println("Host: api.spotify.com");
    client.print("Authorization: Bearer ");
    client.println(access_token);
    client.println("Content-Length: 0");
    client.println("Connection: close");
    client.println();
    client.stop();
}

// =========================================================================
// CONTROLS HANDLER (ENCODERS & BUTTONS)
// =========================================================================
void switchScreen(AppScreen newScreen) {
    currentScreen = newScreen;
    if (currentScreen == SCREEN_MENU) {
        drawMenuScreen(true);
    } else if (currentScreen == SCREEN_SPOTIFY) {
        initSpotifyUI();
    } else if (currentScreen == SCREEN_PC_STATS) {
        drawPCStatsScreen(true);
    } else if (currentScreen == SCREEN_CLOCK) {
        drawClockScreen(true);
    }
}

void checkControls() {
    // 1. Rotate Encoder 1 -> Adjust Spotify Volume
    if (enc1Steps != 0) {
        noInterrupts();
        int steps = enc1Steps;
        enc1Steps = 0;
        interrupts();

        if (currentScreen == SCREEN_SPOTIFY) {
            currentVolume += (steps * 5);
            if (currentVolume > 100) currentVolume = 100;
            if (currentVolume < 0) currentVolume = 0;

            drawVolumeUI();
            volumeNeedsSync = true;
            lastVolumeChangeTime = millis();
        }
    }

    // 2. Rotate Encoder 2 -> Navigate Menu (Up / Down)
    if (enc2Steps != 0) {
        noInterrupts();
        int steps = enc2Steps;
        enc2Steps = 0;
        interrupts();

        if (currentScreen == SCREEN_MENU) {
            if (steps > 0) menuSelection++;
            else menuSelection--;

            if (menuSelection > 2) menuSelection = 0;
            if (menuSelection < 0) menuSelection = 2;

            drawMenuScreen(false); // Redraw menu items only
        }
    }

    // 3. Press Button 1 -> Play / Pause (only active on Spotify screen)
    int sw1 = digitalRead(ENC1_SW);
    if (sw1 != lastSw1) {
        if (sw1 == LOW && (millis() - lastBtn1Time > 300)) {
            if (currentScreen == SCREEN_SPOTIFY) {
                pendingAction = 1;
            }
            lastBtn1Time = millis();
        }
        lastSw1 = sw1;
    }

    // 4. Press Button 2 -> Open Menu OR Confirm Menu Selection
    int sw2 = digitalRead(ENC2_SW);
    if (sw2 != lastSw2) {
        if (sw2 == LOW && (millis() - lastBtn2Time > 300)) {
            lastBtn2Time = millis();
            if (currentScreen != SCREEN_MENU) {
                // Enter menu screen
                menuSelection = (int)currentScreen;
                switchScreen(SCREEN_MENU);
            } else {
                // Confirm selection and switch to chosen screen
                switchScreen((AppScreen)menuSelection);
            }
        }
        lastSw2 = sw2;
    }
}

bool updateCurrentSong() {
    if (WiFi.status() != WL_CONNECTED) return false;
    
    client.setTimeout(3000);
    if (!client.connect(host, httpsPort)) {
        connectionRetries++;
        return false;
    }

    client.println("GET /v1/me/player HTTP/1.1");
    client.println("Host: api.spotify.com");
    client.print("Authorization: Bearer ");
    client.println(access_token);
    client.println("Connection: close");
    client.println();

    unsigned long timeout = millis() + 3000;
    while (client.connected() && millis() < timeout) {
        checkControls();
        if (client.available()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") break;
        }
    }

    if (millis() >= timeout) {
        client.stop();
        connectionRetries++;
        return false;
    }

    String response = client.readString();
    client.stop();

    if (response.length() == 0) return false;

    StaticJsonDocument<256> filter;
    filter["is_playing"] = true;
    filter["progress_ms"] = true;
    filter["item"]["id"] = true;
    filter["item"]["name"] = true;
    filter["item"]["duration_ms"] = true;
    filter["item"]["artists"][0]["name"] = true;
    filter["device"]["volume_percent"] = true;

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, response, DeserializationOption::Filter(filter));

    if (error) return false;

    connectionRetries = 0;
    hasValidData = true;

    String rawTrack = doc["item"]["name"].as<String>();
    String rawArtist = doc["item"]["artists"][0]["name"].as<String>();
    String trackName = prepareRomanianText(rawTrack);
    String artistName = prepareRomanianText(rawArtist);
    String trackId = doc["item"]["id"].as<String>();
    bool newIsPlaying = doc["is_playing"].as<bool>();

    long duration_ms = doc["item"]["duration_ms"].as<long>();
    long progress_ms = doc["progress_ms"].as<long>();

    // Two-way volume sync: update local volume if changed from PC/Phone
    if (!volumeNeedsSync && (millis() - lastVolumeChangeTime > 3000) && !doc["device"]["volume_percent"].isNull()) {
        int serverVol = doc["device"]["volume_percent"].as<int>();
        if (serverVol != currentVolume) {
            currentVolume = serverVol;
            drawVolumeUI();
        }
    }

    unsigned long currentTime = millis();
    bool isNewTrack = (trackId != currentTrackId);
    
    serverProgress = progress_ms;
    trackDuration = duration_ms;
    lastServerUpdate = currentTime;
    if (newIsPlaying) {
        trackStartTime = currentTime - progress_ms;
    }
    
    wasPlayingBeforeUpdate = isPlaying;
    isPlaying = newIsPlaying;

    if (isNewTrack || trackName != lastTrackName || artistName != lastArtistName) {
        currentTrackId = trackId;
        lastTrackName = trackName;
        lastArtistName = artistName;
        
        if (currentScreen == SCREEN_SPOTIFY) {
            tft.fillRect(58, 26, 100, 50, TFT_BLACK);
            resetTitleScroll();

            String dispArtist = artistName;
            if (dispArtist.length() > maxTitleChars) {
                dispArtist = dispArtist.substring(0, maxTitleChars - 3) + "...";
            }
            drawRomanianString(dispArtist, 60, 52, SUBTLE_GREY, TFT_BLACK, maxTitleChars);
        }
    }

    if (currentScreen == SCREEN_SPOTIFY) {
        updateProgressBar();
    }
    return true;
}

void setup() {
    Serial.begin(115200);
    
    pinMode(ENC1_CLK, INPUT_PULLUP);
    pinMode(ENC1_DT, INPUT_PULLUP);
    pinMode(ENC1_SW, INPUT_PULLUP);

    pinMode(ENC2_CLK, INPUT_PULLUP);
    pinMode(ENC2_DT, INPUT_PULLUP);
    pinMode(ENC2_SW, INPUT_PULLUP);

    lastSw1 = digitalRead(ENC1_SW);
    lastSw2 = digitalRead(ENC2_SW);

    attachInterrupt(digitalPinToInterrupt(ENC1_CLK), isrEnc1, FALLING);
    attachInterrupt(digitalPinToInterrupt(ENC2_CLK), isrEnc2, FALLING);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    
    showLoadingScreen("Starting");
    delay(500);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        showLoadingScreen("WiFi");
        delay(300);
    }

    // Configure internet time via NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    showLoadingScreen("Spotify");
    client.setInsecure(); 
    
    access_token = getAccessToken();
    if (access_token.isEmpty()) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(10, 60);
        tft.print("Auth Error");
        delay(3000);
    } else {
        lastTokenTime = millis();
    }

    initSpotifyUI();
    
    if (!access_token.isEmpty()) {
        updateCurrentSong();
    }
}

void loop() {
    static unsigned long lastCheckTime = 0;
    static unsigned long lastProgressUpdate = 0;
    static unsigned long lastClockUpdate = 0;

    // 1. Poll encoder steps and button states
    checkControls();

    // 2. Read incoming PC stats over USB Serial
    readPCSerialData();

    // 3. Handle active screen logic
    if (currentScreen == SCREEN_SPOTIFY) {
        if (hasValidData) {
            updateVinylAnimation();
            updateTitleScroll();
        }

        if (pendingAction == 1) {
            pendingAction = 0;
            if (isPlaying) {
                serverProgress += (millis() - lastServerUpdate);
            }
            const char* cmd = isPlaying ? "/v1/me/player/pause" : "/v1/me/player/play";
            isPlaying = !isPlaying;
            lastServerUpdate = millis();
            updateProgressBar(true);
            sendSpotifyCommand(cmd, "PUT");
            lastCheckTime = millis();
        }

        if (volumeNeedsSync && (millis() - lastVolumeChangeTime > 500)) {
            volumeNeedsSync = false;
            setSpotifyVolume(currentVolume);
            lastCheckTime = millis();
        }

        if (millis() - lastProgressUpdate >= 250 && hasValidData) {
            lastProgressUpdate = millis();
            updateProgressBar();
            updateTimeDisplay();
        }

        if (!volumeNeedsSync && millis() - lastCheckTime >= 4000) {
            lastCheckTime = millis();
            if (connectionRetries >= maxRetries) {
                connectionRetries = 0;
                delay(500);
            } else {
                updateCurrentSong();
            }
        }
    } 
    else if (currentScreen == SCREEN_PC_STATS) {
        if (pcStatsNeedRedraw) {
            pcStatsNeedRedraw = false;
            drawPCStatsScreen(false);
        }
    } 
    else if (currentScreen == SCREEN_CLOCK) {
        if (millis() - lastClockUpdate >= 250) {
            lastClockUpdate = millis();
            drawClockScreen(false);
        }
    }

    // Refresh Spotify OAuth token every hour regardless of active screen
    if (millis() - lastTokenTime > tokenExpireTime) {
        access_token = getAccessToken();
        lastTokenTime = millis();
    }
}
