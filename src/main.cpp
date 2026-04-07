/*
 * ESP32-C3 SuperMini Smart Watch
 * Round GC9A01 240x240 Display
 *
 * Wiring:
 *   GC9A01: SCK=GPIO5, MOSI=GPIO6, DC=GPIO7, CS=GPIO8, RST=GPIO9
 *   Buttons: BACK=GPIO0, SETTINGS=GPIO1, NEXT=GPIO2 (INPUT_PULLDOWN, active HIGH)
 *   Buzzer: GPIO3 (passive piezo, PWM) — RTC GPIO, stays LOW during boot
 *
 * Screens:
 *   1) Watch face — time, Gregorian date, Islamic (Hijri) date
 *   2) Weather — current conditions + 3-day forecast (Lynnwood, WA via Open-Meteo)
 *   3) Timer — time display, alarm, stopwatch
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <math.h>
#include "secrets.h"

// ── Pin Definitions ──────────────────────────────────────────────
#define BTN_BACK     0
#define BTN_SETTINGS 1
#define BTN_NEXT     2
#define BUZZER_PIN   3
#define BUZZER_CH    0  // LEDC channel for buzzer

// ── Lynnwood, WA coordinates for Open-Meteo ────────────────────
const float LATITUDE  = 47.8209;
const float LONGITUDE = -122.3151;

// Pacific Time (Lynnwood, WA): UTC-8, DST UTC-7
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET = -8 * 3600;
const int   DST_OFFSET = 3600;

// ── Display ─────────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

// ── Colors (RGB565) ─────────────────────────────────────────────
#define COL_BG         0x0000
#define COL_WHITE      0xFFFF
#define COL_CYAN       0x07FF
#define COL_YELLOW     0xFFE0
#define COL_ORANGE     0xFD20
#define COL_GREEN      0x07E0
#define COL_RED        0xF800
#define COL_MAGENTA    0xF81F
#define COL_BLUE       0x001F
#define COL_LIGHT_BLUE 0x653F
#define COL_DARK_GRAY  0x4208
#define COL_MID_GRAY   0x8410
#define COL_GOLD       0xFEA0
#define COL_TEAL       0x0410

// ── Screen State ────────────────────────────────────────────────
enum Screen { SCREEN_CLOCK = 0, SCREEN_WEATHER, SCREEN_TIMER, SCREEN_GAME, SCREEN_POMODORO, SCREEN_COUNT };
Screen currentScreen = SCREEN_CLOCK;

// ── Button Debounce ─────────────────────────────────────────────
struct Button {
    uint8_t pin;
    bool lastState;
    bool pressed;
    unsigned long lastDebounce;
    unsigned long holdStart;
    bool holding;
    bool holdFired;
};
Button buttons[] = {
    {BTN_BACK, false, false, 0, 0, false, false},
    {BTN_SETTINGS, false, false, 0, 0, false, false},
    {BTN_NEXT, false, false, 0, 0, false, false}
};
const unsigned long DEBOUNCE_MS = 200;
const unsigned long HOLD_MS = 1000; // Long-press threshold

// ── Buzzer Chimes ───────────────────────────────────────────────
void buzzerTone(uint32_t freq, uint32_t durationMs) {
    ledcWriteTone(BUZZER_CH, freq);
    delay(durationMs);
    ledcWriteTone(BUZZER_CH, 0);
}

void chimeBack() {
    buzzerTone(880, 60);
    delay(20);
    buzzerTone(660, 60);
}

void chimeSettings() {
    buzzerTone(1047, 50);
    delay(20);
    buzzerTone(1319, 50);
    delay(20);
    buzzerTone(1568, 50);
}

void chimeNext() {
    buzzerTone(1200, 80);
}

void chimeAlarm() {
    // Legacy — not used for continuous alarm anymore
}

// Pomodoro: time for a break — pleasant descending melody
void chimeBreakStart() {
    buzzerTone(1047, 150); // C6
    delay(30);
    buzzerTone(988, 150);  // B5
    delay(30);
    buzzerTone(784, 150);  // G5
    delay(30);
    buzzerTone(659, 200);  // E5
    delay(50);
    buzzerTone(523, 300);  // C5 — resolve, held long
}

// Pomodoro: break over — energetic ascending fanfare
void chimeBreakEnd() {
    buzzerTone(523, 100);  // C5
    delay(20);
    buzzerTone(659, 100);  // E5
    delay(20);
    buzzerTone(784, 100);  // G5
    delay(20);
    buzzerTone(1047, 150); // C6
    delay(40);
    buzzerTone(1319, 150); // E6
    delay(40);
    buzzerTone(1568, 250); // G6 — bright finish
}

// Game: death crash
void chimeDeath() {
    buzzerTone(400, 80);
    delay(30);
    buzzerTone(250, 80);
    delay(30);
    buzzerTone(150, 150);
}

// Game: jump blip
void chimeJump() {
    ledcWriteTone(BUZZER_CH, 900);
    delay(30);
    ledcWriteTone(BUZZER_CH, 0);
}

// ── Weather Data ────────────────────────────────────────────────
struct WeatherData {
    float temp;
    float feelsLike;
    int humidity;
    int weatherCode;
    float windSpeed;
};

struct ForecastDay {
    String dayName;
    float tempMin;
    float tempMax;
    int weatherCode;
};

WeatherData currentWeather;
ForecastDay forecast[3];
bool weatherValid = false;
unsigned long lastWeatherFetch = 0;
const unsigned long WEATHER_INTERVAL = 600000; // 10 minutes

// ── Stopwatch ───────────────────────────────────────────────────
bool stopwatchRunning = false;
unsigned long stopwatchStart = 0;
unsigned long stopwatchElapsed = 0;

// ── Alarm ───────────────────────────────────────────────────────
bool alarmEnabled = false;
int alarmHour = 7;
int alarmMinute = 0;
bool alarmTriggered = false;       // prevents re-trigger within same minute
bool alarmRinging = false;         // alarm is actively sounding
unsigned long alarmLastChime = 0;  // for repeating chime (non-blocking)
int alarmNoteIdx = 0;              // current note in alarm melody
int alarmSnoozeMin = 5;            // snooze duration in minutes (0 = disabled)
bool alarmSnoozing = false;        // snooze countdown active
unsigned long alarmSnoozeEnd = 0;  // millis() when snooze expires
Screen alarmPrevScreen = SCREEN_CLOCK; // screen to restore after alarm dismiss
// Forward declarations for alarm actions (defined near checkAlarm)
void alarmStop();
void alarmSnooze();

// ── Timer Screen Sub-modes ──────────────────────────────────────
enum TimerMode { TIMER_CLOCK = 0, TIMER_ALARM, TIMER_STOPWATCH, TIMER_MODE_COUNT };
TimerMode timerMode = TIMER_CLOCK;

// ── Pomodoro ───────────────────────────────────────────────────
int pomoWorkMin = 25;
int pomoBreakMin = 5;
enum PomoState { POMO_IDLE, POMO_WORK, POMO_BREAK };
PomoState pomoState = POMO_IDLE;
bool pomoRunning = false;
unsigned long pomoTimeLeft = 0;   // ms remaining in current phase
unsigned long pomoLastTick = 0;   // last millis() snapshot for countdown
bool pomoInSettings = false;
int pomoField = 0;               // 0=work, 1=break
bool pomoEditing = false;

// ── Clock Manual Time Setting (when WiFi unavailable) ──────────
bool clockInSettings = false;
int clockField = 0; // 0=hour, 1=min, 2=month, 3=day, 4=year
int clockSetHour, clockSetMin, clockSetMonth, clockSetDay, clockSetYear;

// ── Settings State ──────────────────────────────────────────────
bool inSettings = false;
int settingsField = 0; // 0=hour, 1=minute, 2=enable/disable

// ── Geometry Dash Game State ────────────────────────────────────
enum GameState { GAME_READY, GAME_PLAYING, GAME_OVER, GAME_CONFIRM_EXIT };
GameState gameState = GAME_READY;

// Ground & play area (circular display — usable band roughly y=40..200)
#define GAME_GROUND_Y   178          // ground line y
#define GAME_PLAYER_X    55          // player square center-x
#define GAME_PLAYER_SZ   14          // half-size of player square
#define GAME_GRAVITY    1.0f         // pixels/frame² downward
#define GAME_JUMP_VEL  -8.8f         // initial upward velocity
#define GAME_MAX_OBS     6           // max simultaneous obstacles
#define GAME_MAX_PARTS  20           // particle pool
#define GAME_MAX_STARS  18           // background stars

float  gamePlayerY  = GAME_GROUND_Y - GAME_PLAYER_SZ * 2;
float  gameVelY     = 0;
bool   gameOnGround = true;
bool   gameCanDouble = false;        // double-jump available
float  gamePlayerRot = 0;            // rotation angle (radians)
float  gameSquash    = 1.0f;         // squash/stretch factor (1=normal)
unsigned long gameLandMs = 0;        // landing timestamp for squash anim

// Obstacle types: 0=single spike, 1=double spike, 2=tall spike
struct Obstacle {
    float x;
    int   halfW;
    int   h;
    int   type;       // 0=single, 1=double, 2=tall
    bool  active;
    bool  scored;
};
Obstacle gameObs[GAME_MAX_OBS];

// Particle system (trail + death explosion)
struct Particle {
    float x, y, vx, vy;
    uint16_t col;
    int life;         // frames remaining
    bool active;
};
Particle gameParts[GAME_MAX_PARTS];

// Background stars
struct Star {
    float x, y;
    uint16_t col;
    float speed;      // relative to gameSpeed
};
Star gameStars[GAME_MAX_STARS];

float  gameSpeed     = 2.8f;        // obstacle scroll speed (px/frame)
int    gameScore     = 0;
int    gameHiScore   = 0;
unsigned long gameFrameMs = 0;
float  gameSpawnDist = 0;
int    gameFlashTimer = 0;           // milestone flash countdown
uint16_t gameFlashCol = COL_WHITE;   // milestone flash color

// Color theme progression based on score
uint16_t gameGroundCol  = COL_DARK_GRAY;
uint16_t gamePlayerCol  = COL_GREEN;
uint16_t gameAccentCol  = COL_CYAN;
uint16_t gameBgCol      = 0x0000;      // tinted background color

// Background scrolling columns (far layer)
#define GAME_MAX_COLS    6
struct BgColumn {
    float x;
    int   w, h;       // width and height from ground up
    uint16_t col;
    float speed;       // parallax factor (0..1, lower=farther)
};
BgColumn gameBgCols[GAME_MAX_COLS];
float gameBgScroll = 0; // accumulated scroll for ground perspective

// ── Hijri Calendar Conversion ───────────────────────────────────
struct HijriDate {
    int year;
    int month;
    int day;
};

const char* hijriMonths[] = {
    "Muharram", "Safar", "Rabi al-Awwal", "Rabi al-Thani",
    "Jumada al-Ula", "Jumada al-Thani", "Rajab", "Sha'ban",
    "Ramadan", "Shawwal", "Dhul Qi'dah", "Dhul Hijjah"
};

HijriDate gregorianToHijri(int gYear, int gMonth, int gDay) {
    int a = (14 - gMonth) / 12;
    int y = gYear + 4800 - a;
    int m = gMonth + 12 * a - 3;
    long jd = gDay + (153 * m + 2) / 5 + 365L * y + y / 4 - y / 100 + y / 400 - 32045;

    long l = jd - 1948440 + 10632;
    long n = (l - 1) / 10631;
    l = l - 10631 * n + 354;

    long j = ((long)(10985 - l) / 5316) * ((long)(50 * l) / 17719)
           + ((long)l / 5670) * ((long)(43 * l) / 15238);
    l = l - ((long)(30 - j) / 15) * ((long)(17719 * j) / 50)
           - ((long)j / 16) * ((long)(15238 * j) / 43) + 29;

    int hMonth = (int)(24 * l / 709);
    int hDay   = (int)(l - (long)(709 * hMonth) / 24);
    int hYear  = (int)(30 * n + j - 30);

    return {hYear, hMonth, hDay};
}

// ── WMO Weather Code to Description & Color ─────────────────────
const char* wmoDescription(int code) {
    if (code == 0) return "Clear";
    if (code == 1) return "Mostly Clear";
    if (code == 2) return "Partly Cloudy";
    if (code == 3) return "Overcast";
    if (code == 45 || code == 48) return "Foggy";
    if (code == 51 || code == 53 || code == 55) return "Drizzle";
    if (code == 56 || code == 57) return "Frzg Drizzle";
    if (code == 61 || code == 63 || code == 65) return "Rain";
    if (code == 66 || code == 67) return "Frzg Rain";
    if (code == 71 || code == 73 || code == 75) return "Snow";
    if (code == 77) return "Snow Grains";
    if (code == 80 || code == 81 || code == 82) return "Showers";
    if (code == 85 || code == 86) return "Snow Showers";
    if (code == 95) return "Thunderstorm";
    if (code == 96 || code == 99) return "T-Storm Hail";
    return "Unknown";
}

uint16_t wmoColor(int code) {
    if (code <= 1) return COL_YELLOW;
    if (code <= 3) return COL_LIGHT_BLUE;
    if (code <= 48) return COL_MID_GRAY;
    if (code <= 57) return COL_BLUE;
    if (code <= 67) return COL_BLUE;
    if (code <= 77) return COL_WHITE;
    if (code <= 82) return COL_BLUE;
    if (code <= 86) return COL_WHITE;
    if (code >= 95) return COL_MAGENTA;
    return COL_WHITE;
}

// ── Weather Icons (drawn with primitives) ───────────────────────
void drawCloud(int cx, int cy, int r) {
    spr.fillCircle(cx, cy - r/2, r, COL_WHITE);
    spr.fillCircle(cx - r, cy, r * 3/4, COL_WHITE);
    spr.fillCircle(cx + r, cy, r * 3/4, COL_WHITE);
    spr.fillRect(cx - r, cy - r/4, r * 2, r, COL_WHITE);
}

void drawWeatherIcon(int cx, int cy, int code, int s) {
    // s = scale: 1 for small (forecast), 2 for larger (current weather)
    if (code == 0) {
        // Clear — sun with rays
        int r = 3 * s;
        spr.fillCircle(cx, cy, r, COL_YELLOW);
        for (int i = 0; i < 8; i++) {
            float a = i * 45.0 * DEG_TO_RAD;
            int x1 = cx + (int)((r + 2) * cos(a));
            int y1 = cy + (int)((r + 2) * sin(a));
            int x2 = cx + (int)((r + 2 + 2 * s) * cos(a));
            int y2 = cy + (int)((r + 2 + 2 * s) * sin(a));
            spr.drawLine(x1, y1, x2, y2, COL_YELLOW);
        }
    } else if (code <= 2) {
        // Partly cloudy — sun peeking behind cloud
        int r = 2 * s;
        spr.fillCircle(cx + r + s, cy - r, r, COL_YELLOW);
        for (int i = 0; i < 8; i++) {
            float a = i * 45.0 * DEG_TO_RAD;
            int x1 = cx + r + s + (int)((r + 1) * cos(a));
            int y1 = cy - r + (int)((r + 1) * sin(a));
            int x2 = cx + r + s + (int)((r + 1 + s) * cos(a));
            int y2 = cy - r + (int)((r + 1 + s) * sin(a));
            spr.drawLine(x1, y1, x2, y2, COL_YELLOW);
        }
        drawCloud(cx - s, cy, r);
    } else if (code == 3) {
        // Overcast — cloud
        drawCloud(cx, cy, 3 * s);
    } else if (code == 45 || code == 48) {
        // Fog — horizontal lines
        for (int i = 0; i < 3; i++) {
            int ly = cy - 3 * s + i * 3 * s;
            spr.drawFastHLine(cx - 5 * s, ly, 10 * s, COL_MID_GRAY);
        }
    } else if ((code >= 51 && code <= 57) || (code >= 61 && code <= 67) || (code >= 80 && code <= 82)) {
        // Rain / drizzle / showers — cloud + rain drops
        int r = 2 * s;
        drawCloud(cx, cy - r, r);
        uint16_t dropCol = COL_BLUE;
        for (int d = -1; d <= 1; d++) {
            int dx = cx + d * 3 * s;
            spr.drawLine(dx, cy + s, dx - s, cy + s + 2 * s, dropCol);
        }
    } else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
        // Snow — cloud + snow dots
        int r = 2 * s;
        drawCloud(cx, cy - r, r);
        spr.fillCircle(cx - 2 * s, cy + s + s, s, COL_CYAN);
        spr.fillCircle(cx, cy + 2 * s + s, s, COL_CYAN);
        spr.fillCircle(cx + 2 * s, cy + s + s, s, COL_CYAN);
    } else if (code >= 95) {
        // Thunderstorm — cloud + lightning bolt
        int r = 2 * s;
        drawCloud(cx, cy - r, r);
        spr.drawLine(cx, cy + s, cx - s, cy + 2 * s, COL_YELLOW);
        spr.drawLine(cx - s, cy + 2 * s, cx + s / 2, cy + 2 * s, COL_YELLOW);
        spr.drawLine(cx + s / 2, cy + 2 * s, cx - s / 2, cy + 3 * s, COL_YELLOW);
    }
}

// ── WiFi Connection ─────────────────────────────────────────────
void connectWiFi() {
    printf("[WiFi] Starting connection...\n");
    printf("[WiFi] SSID: %s\n", WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    tft.fillScreen(COL_BG);
    tft.setTextColor(COL_CYAN, COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Connecting", 120, 110, 4);
    tft.drawString("WiFi...", 120, 140, 4);

    unsigned long startMs = millis();
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < 120000) {
        delay(500);
        attempts++;
        printf("[WiFi] Attempt %d — %lus elapsed — status: %d\n",
                      attempts, (millis() - startMs) / 1000, WiFi.status());
        tft.fillCircle(120, 170, 4, (attempts % 2) ? COL_CYAN : COL_BG);
    }

    if (WiFi.status() == WL_CONNECTED) {
        printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        printf("[WiFi] RSSI: %d dBm\n", WiFi.RSSI());
        tft.fillScreen(COL_BG);
        tft.setTextColor(COL_GREEN, COL_BG);
        tft.drawString("Connected!", 120, 120, 4);
        delay(1000);
    } else {
        printf("[WiFi] Failed after %d attempts, status: %d\n", attempts, WiFi.status());
        tft.fillScreen(COL_BG);
        tft.setTextColor(COL_RED, COL_BG);
        tft.drawString("WiFi Failed", 120, 120, 4);
        delay(2000);
    }
}

// ── NTP Time Sync ───────────────────────────────────────────────
void syncTime() {
    if (WiFi.status() == WL_CONNECTED) {
        configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
        struct tm t;
        if (!getLocalTime(&t, 10000)) {
            printf("[NTP] Sync failed, defaulting to noon\n");
            struct timeval tv = { .tv_sec = 43200, .tv_usec = 0 }; // 12:00:00
            settimeofday(&tv, NULL);
        } else {
            printf("[NTP] Synced: %02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
        }
    } else {
        printf("[NTP] No WiFi, defaulting to noon\n");
        struct timeval tv = { .tv_sec = 43200, .tv_usec = 0 }; // 12:00:00
        settimeofday(&tv, NULL);
    }
}

// ── Fetch Weather (Open-Meteo — no API key) ─────────────────────
void fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    // Open-Meteo API: current weather + 3-day forecast, Fahrenheit, mph
    String url = "http://api.open-meteo.com/v1/forecast?"
                 "latitude=" + String(LATITUDE, 4) +
                 "&longitude=" + String(LONGITUDE, 4) +
                 "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m"
                 "&daily=weather_code,temperature_2m_max,temperature_2m_min"
                 "&temperature_unit=fahrenheit"
                 "&wind_speed_unit=mph"
                 "&timezone=America%2FLos_Angeles"
                 "&forecast_days=4";

    http.begin(url);
    int code = http.GET();

    if (code == 200) {
        String payload = http.getString();
        DynamicJsonDocument doc(4096);
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            printf("JSON parse error: %s\n", err.c_str());
            http.end();
            return;
        }

        // Current weather
        JsonObject current = doc["current"];
        currentWeather.temp        = current["temperature_2m"];
        currentWeather.feelsLike   = current["apparent_temperature"];
        currentWeather.humidity    = current["relative_humidity_2m"];
        currentWeather.weatherCode = current["weather_code"];
        currentWeather.windSpeed   = current["wind_speed_10m"];

        // Daily forecast (skip today = index 0, take next 3)
        JsonArray dailyCodes   = doc["daily"]["weather_code"];
        JsonArray dailyMaxTemp = doc["daily"]["temperature_2m_max"];
        JsonArray dailyMinTemp = doc["daily"]["temperature_2m_min"];
        JsonArray dailyTime    = doc["daily"]["time"];

        for (int i = 0; i < 3 && i + 1 < (int)dailyCodes.size(); i++) {
            forecast[i].weatherCode = dailyCodes[i + 1];
            forecast[i].tempMax     = dailyMaxTemp[i + 1];
            forecast[i].tempMin     = dailyMinTemp[i + 1];

            // Parse date string "YYYY-MM-DD" to get day name
            String dateStr = dailyTime[i + 1].as<String>();
            int yr = dateStr.substring(0, 4).toInt();
            int mo = dateStr.substring(5, 7).toInt();
            int dy = dateStr.substring(8, 10).toInt();
            // Zeller-like day of week via mktime
            struct tm tm = {};
            tm.tm_year = yr - 1900;
            tm.tm_mon  = mo - 1;
            tm.tm_mday = dy;
            mktime(&tm);
            const char* days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
            forecast[i].dayName = days[tm.tm_wday];
        }

        weatherValid = true;
    }
    http.end();
}

void updateWeather() {
    unsigned long now = millis();
    if (now - lastWeatherFetch > WEATHER_INTERVAL || lastWeatherFetch == 0) {
        fetchWeather();
        lastWeatherFetch = now;
    }
}

// ── Button Handling ─────────────────────────────────────────────
void updateButtons() {
    unsigned long now = millis();
    for (int i = 0; i < 3; i++) {
        bool state = !digitalRead(buttons[i].pin); // Invert: LOW=pressed, HIGH=released
        buttons[i].pressed = false;
        if (state && !buttons[i].lastState && (now - buttons[i].lastDebounce > DEBOUNCE_MS)) {
            buttons[i].pressed = true;
            buttons[i].lastDebounce = now;
            buttons[i].holdStart = now;
            buttons[i].holdFired = false;
        }
        buttons[i].holding = state;
        if (!state) buttons[i].holdFired = false;
        buttons[i].lastState = state;
    }
}

// ── Draw Helper Functions ───────────────────────────────────────
void drawCircleBorder() {
    spr.drawCircle(120, 120, 118, COL_DARK_GRAY);
    spr.drawCircle(120, 120, 119, COL_DARK_GRAY);
}

void drawScreenIndicator(int active) {
    int count = SCREEN_COUNT;
    int startX = 120 - (count - 1) * 6; // center the dots
    for (int i = 0; i < count; i++) {
        int x = startX + i * 12;
        int y = 228;
        if (i == active) {
            spr.fillCircle(x, y, 4, COL_CYAN);
        } else {
            spr.drawCircle(x, y, 3, COL_DARK_GRAY);
        }
    }
}

// ── Helper: days in month ───────────────────────────────────────
int daysInMonth(int month, int year) {
    const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return d[month - 1];
}

// ── Helper: apply manual time setting ──────────────────────────
void applyClockSettings() {
    struct tm tm = {};
    tm.tm_hour = clockSetHour;
    tm.tm_min  = clockSetMin;
    tm.tm_sec  = 0;
    tm.tm_mon  = clockSetMonth - 1;
    tm.tm_mday = clockSetDay;
    tm.tm_year = clockSetYear - 1900;
    time_t epoch = mktime(&tm);
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

// ── Screen 1: Clock Face ────────────────────────────────────────
void drawClockScreen() {
    struct tm t;
    getLocalTime(&t, 100);

    spr.fillSprite(COL_BG);
    drawCircleBorder();
    drawScreenIndicator(0);
    spr.setTextDatum(MC_DATUM);

    if (clockInSettings) {
        // ── Manual time/date setting screen ──
        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString("Set Time & Date", 120, 25, 2);

        const char* labels[] = {"Hour", "Minute", "Month", "Day", "Year"};
        int values[] = {clockSetHour, clockSetMin, clockSetMonth, clockSetDay, clockSetYear};

        // Time row: HH : MM
        char hBuf[3], mBuf[3];
        sprintf(hBuf, "%02d", clockSetHour);
        sprintf(mBuf, "%02d", clockSetMin);

        spr.setTextColor(clockField == 0 ? COL_GREEN : COL_WHITE, COL_BG);
        spr.drawString(hBuf, 90, 70, 7);
        spr.setTextColor(COL_MID_GRAY, COL_BG);
        spr.drawString(":", 120, 65, 6);
        spr.setTextColor(clockField == 1 ? COL_GREEN : COL_WHITE, COL_BG);
        spr.drawString(mBuf, 150, 70, 7);

        // Date row
        const char* monthNames[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};
        char dateBuf[20];
        sprintf(dateBuf, "%s %d, %d", monthNames[clockSetMonth - 1], clockSetDay, clockSetYear);

        // Highlight active date field
        if (clockField == 2) {
            spr.setTextColor(COL_GREEN, COL_BG);
            spr.drawString(monthNames[clockSetMonth - 1], 65, 120, 4);
            spr.setTextColor(COL_WHITE, COL_BG);
            char restBuf[12];
            sprintf(restBuf, "%d, %d", clockSetDay, clockSetYear);
            spr.drawString(restBuf, 155, 120, 4);
        } else if (clockField == 3) {
            spr.setTextColor(COL_WHITE, COL_BG);
            spr.drawString(monthNames[clockSetMonth - 1], 65, 120, 4);
            spr.setTextColor(COL_GREEN, COL_BG);
            char dayBuf[3];
            sprintf(dayBuf, "%d", clockSetDay);
            spr.drawString(dayBuf, 120, 120, 4);
            spr.setTextColor(COL_WHITE, COL_BG);
            char yrBuf[6];
            sprintf(yrBuf, "%d", clockSetYear);
            spr.drawString(yrBuf, 175, 120, 4);
        } else if (clockField == 4) {
            spr.setTextColor(COL_WHITE, COL_BG);
            char preBuf[10];
            sprintf(preBuf, "%s %d,", monthNames[clockSetMonth - 1], clockSetDay);
            spr.drawString(preBuf, 90, 120, 4);
            spr.setTextColor(COL_GREEN, COL_BG);
            char yrBuf[6];
            sprintf(yrBuf, "%d", clockSetYear);
            spr.drawString(yrBuf, 175, 120, 4);
        } else {
            spr.setTextColor(COL_WHITE, COL_BG);
            spr.drawString(dateBuf, 120, 120, 4);
        }

        // Active field label
        spr.setTextColor(COL_YELLOW, COL_BG);
        char fieldLabel[20];
        sprintf(fieldLabel, "< > %s", labels[clockField]);
        spr.drawString(fieldLabel, 120, 160, 2);

        spr.setTextColor(COL_MID_GRAY, COL_BG);
        spr.drawString("SET=Field NEXT=+ BACK=Done", 120, 195, 1);

        spr.pushSprite(0, 0);
        return;
    }

    // ── Normal clock face ──
    // Time — large centered
    char timeBuf[6];
    int hour12 = t.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    sprintf(timeBuf, "%d:%02d", hour12, t.tm_min);
    spr.setTextColor(COL_WHITE, COL_BG);
    spr.drawString(timeBuf, 115, 65, 7);

    // AM/PM indicator — sun or moon icon (left of time)
    int ix = 42, iy = 65;
    if (t.tm_hour < 12) {
        // Sun: filled circle + rays
        spr.fillCircle(ix, iy, 5, COL_YELLOW);
        for (int i = 0; i < 8; i++) {
            float a = i * 45.0 * DEG_TO_RAD;
            spr.drawLine(ix + (int)(7 * cos(a)), iy + (int)(7 * sin(a)),
                         ix + (int)(10 * cos(a)), iy + (int)(10 * sin(a)), COL_YELLOW);
        }
    } else {
        // Crescent moon: filled circle with a dark circle overlapping
        spr.fillCircle(ix, iy, 6, COL_CYAN);
        spr.fillCircle(ix + 4, iy - 3, 5, COL_BG);
    }

    // Seconds arc
    for (int s = 0; s <= t.tm_sec; s++) {
        float a = s * 6.0 - 90.0;
        float rad = a * DEG_TO_RAD;
        int x = 120 + (int)(105 * cos(rad));
        int y = 120 + (int)(105 * sin(rad));
        spr.fillCircle(x, y, 2, COL_TEAL);
    }

    // Gregorian date
    const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                            "Jul","Aug","Sep","Oct","Nov","Dec"};
    const char* days[] = {"Sunday","Monday","Tuesday","Wednesday",
                          "Thursday","Friday","Saturday"};

    spr.setTextColor(COL_YELLOW, COL_BG);
    spr.drawString(days[t.tm_wday], 120, 105, 2);

    char dateBuf[20];
    sprintf(dateBuf, "%s %d, %d", months[t.tm_mon], t.tm_mday, t.tm_year + 1900);
    spr.setTextColor(COL_GREEN, COL_BG);
    spr.drawString(dateBuf, 120, 130, 4);

    // Hijri date
    HijriDate h = gregorianToHijri(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    char hijriBuf[40];
    sprintf(hijriBuf, "%d %s %d", h.day, hijriMonths[h.month - 1], h.year);
    spr.setTextColor(COL_GOLD, COL_BG);
    spr.drawString(hijriBuf, 120, 160, 2);

    // Divider line
    spr.drawFastHLine(50, 178, 140, COL_DARK_GRAY);

    // WiFi status & temperature summary at bottom
    if (weatherValid) {
        char tempBuf[20];
        sprintf(tempBuf, "%.0f°F %s", currentWeather.temp, wmoDescription(currentWeather.weatherCode));
        spr.setTextColor(COL_LIGHT_BLUE, COL_BG);
        spr.drawString(tempBuf, 120, 195, 2);
    }

    spr.setTextColor(WiFi.status() == WL_CONNECTED ? COL_GREEN : COL_RED, COL_BG);
    spr.drawString(WiFi.status() == WL_CONNECTED ? "WiFi" : "No WiFi", 120, 212, 1);

    spr.pushSprite(0, 0);
}

// ── Screen 2: Weather ───────────────────────────────────────────
void drawWeatherScreen() {
    spr.fillSprite(COL_BG);
    drawCircleBorder();
    drawScreenIndicator(1);

    spr.setTextDatum(MC_DATUM);

    if (!weatherValid) {
        spr.setTextColor(COL_YELLOW, COL_BG);
        spr.drawString("Fetching", 120, 100, 4);
        spr.drawString("Weather...", 120, 130, 4);
        spr.pushSprite(0, 0);
        return;
    }

    // Current weather header
    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString("Lynnwood, WA", 120, 20, 2);

    // Current temp — big, with unit in smaller font
    char tempNum[8];
    sprintf(tempNum, "%.0f", currentWeather.temp);
    int numW = spr.textWidth(tempNum, 6);
    int unitW = spr.textWidth("F", 4);
    int tempTotalW = numW + 2 + unitW;
    int tempStartX = 120 - tempTotalW / 2;
    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(COL_WHITE, COL_BG);
    spr.drawString(tempNum, tempStartX, 52, 6);
    spr.setTextColor(COL_MID_GRAY, COL_BG);
    spr.drawString("F", tempStartX + numW + 2, 52, 4);
    spr.setTextDatum(MC_DATUM);

    // Description with icon — centered as a pair
    const char* condText = wmoDescription(currentWeather.weatherCode);
    int textW = spr.textWidth(condText, 2);
    int iconW = 20; // approx icon width at scale 2
    int totalW = iconW + 4 + textW; // icon + gap + text
    int startX = 120 - totalW / 2;
    drawWeatherIcon(startX + iconW / 2, 82, currentWeather.weatherCode, 2);
    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(wmoColor(currentWeather.weatherCode), COL_BG);
    spr.drawString(condText, startX + iconW + 4, 82, 2);
    spr.setTextDatum(MC_DATUM);

    // Details row
    char detailBuf[30];
    sprintf(detailBuf, "Feels %.0f°  Hum %d%%", currentWeather.feelsLike, currentWeather.humidity);
    spr.setTextColor(COL_MID_GRAY, COL_BG);
    spr.drawString(detailBuf, 120, 100, 2);

    sprintf(detailBuf, "Wind %.0f mph", currentWeather.windSpeed);
    spr.drawString(detailBuf, 120, 115, 2);

    // Divider
    spr.drawFastHLine(30, 130, 180, COL_DARK_GRAY);

    // 3-day forecast
    spr.setTextColor(COL_YELLOW, COL_BG);
    spr.drawString("3-Day Forecast", 120, 142, 2);

    for (int i = 0; i < 3; i++) {
        int y = 162 + i * 24;

        spr.setTextDatum(ML_DATUM);
        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString(forecast[i].dayName, 50, y, 2);

        // Weather condition icon (replaces text)
        drawWeatherIcon(120, y, forecast[i].weatherCode, 1);

        char fcBuf[15];
        sprintf(fcBuf, "%.0f/%.0f°", forecast[i].tempMax, forecast[i].tempMin);
        spr.setTextDatum(MR_DATUM);
        spr.setTextColor(COL_ORANGE, COL_BG);
        spr.drawString(fcBuf, 195, y, 2);
    }

    spr.pushSprite(0, 0);
}

// ── Screen 3: Timer / Alarm / Stopwatch ─────────────────────────
void drawTimerScreen() {
    struct tm t;
    getLocalTime(&t, 100);

    spr.fillSprite(COL_BG);
    drawCircleBorder();
    drawScreenIndicator(2);

    spr.setTextDatum(MC_DATUM);

    // Sub-mode tabs
    const char* tabs[] = {"Clock", "Alarm", "Stopwatch"};
    for (int i = 0; i < 3; i++) {
        int x = 55 + i * 65;
        if (i == (int)timerMode) {
            spr.setTextColor(COL_CYAN, COL_BG);
            spr.fillRoundRect(x - 28, 12, 56, 18, 4, COL_DARK_GRAY);
            spr.drawString(tabs[i], x, 21, 2);
        } else {
            spr.setTextColor(COL_MID_GRAY, COL_BG);
            spr.drawString(tabs[i], x, 21, 2);
        }
    }

    if (timerMode == TIMER_CLOCK) {
        const int cx = 120, cy = 120;

        // Tick marks and hour numbers
        for (int i = 0; i < 60; i++) {
            float a = i * 6.0 - 90.0;
            float rad = a * DEG_TO_RAD;
            int r1 = (i % 5 == 0) ? 88 : 95;
            int r2 = 100;
            uint16_t col = (i % 5 == 0) ? COL_CYAN : COL_DARK_GRAY;
            int x1 = cx + (int)(r1 * cos(rad));
            int y1 = cy + (int)(r1 * sin(rad));
            int x2 = cx + (int)(r2 * cos(rad));
            int y2 = cy + (int)(r2 * sin(rad));
            spr.drawLine(x1, y1, x2, y2, col);

            // Hour numbers at 5-minute marks
            if (i % 5 == 0) {
                int hr = i / 5;
                if (hr == 0) hr = 12;
                char hBuf[3];
                sprintf(hBuf, "%d", hr);
                int tx = cx + (int)(78 * cos(rad));
                int ty = cy + (int)(78 * sin(rad));
                spr.setTextColor(COL_WHITE, COL_BG);
                spr.drawString(hBuf, tx, ty, 2);
            }
        }

        // Hour hand
        float hAngle = ((t.tm_hour % 12) + t.tm_min / 60.0) * 30.0 - 90.0;
        float hRad = hAngle * DEG_TO_RAD;
        int hLen = 45;
        int hx = cx + (int)(hLen * cos(hRad));
        int hy = cy + (int)(hLen * sin(hRad));
        spr.drawLine(cx, cy, hx, hy, COL_WHITE);
        spr.drawLine(cx + 1, cy, hx + 1, hy, COL_WHITE);
        spr.drawLine(cx, cy + 1, hx, hy + 1, COL_WHITE);

        // Minute hand
        float mAngle = t.tm_min * 6.0 - 90.0;
        float mRad = mAngle * DEG_TO_RAD;
        int mLen = 62;
        int mx = cx + (int)(mLen * cos(mRad));
        int my = cy + (int)(mLen * sin(mRad));
        spr.drawLine(cx, cy, mx, my, COL_CYAN);
        spr.drawLine(cx + 1, cy, mx + 1, my, COL_CYAN);

        // Second hand
        float sAngle = t.tm_sec * 6.0 - 90.0;
        float sRad = sAngle * DEG_TO_RAD;
        int sLen = 68;
        int sx = cx + (int)(sLen * cos(sRad));
        int sy = cy + (int)(sLen * sin(sRad));
        spr.drawLine(cx, cy, sx, sy, COL_RED);

        // Center dot
        spr.fillCircle(cx, cy, 3, COL_WHITE);

        // AM/PM sun or moon icon — upper half center
        int ix = cx, iy = cy - 45;
        if (t.tm_hour < 12) {
            spr.fillCircle(ix, iy, 5, COL_YELLOW);
            for (int r = 0; r < 8; r++) {
                float a = r * 45.0 * DEG_TO_RAD;
                spr.drawLine(ix + (int)(7 * cos(a)), iy + (int)(7 * sin(a)),
                             ix + (int)(10 * cos(a)), iy + (int)(10 * sin(a)), COL_YELLOW);
            }
        } else {
            spr.fillCircle(ix, iy, 6, COL_CYAN);
            spr.fillCircle(ix + 4, iy - 3, 5, COL_BG);
        }

        // Compact date display below center
        const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                "Jul","Aug","Sep","Oct","Nov","Dec"};
        const char* dayAbbr[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        char dateBuf[20];
        sprintf(dateBuf, "%s %s %d", dayAbbr[t.tm_wday], months[t.tm_mon], t.tm_mday);
        spr.setTextColor(COL_GREEN, COL_BG);
        spr.drawString(dateBuf, cx, cy + 30, 1);

        HijriDate h = gregorianToHijri(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
        char hijriBuf[30];
        sprintf(hijriBuf, "%d %s %d", h.day, hijriMonths[h.month - 1], h.year);
        spr.setTextColor(COL_GOLD, COL_BG);
        spr.drawString(hijriBuf, cx, cy + 42, 1);

    } else if (timerMode == TIMER_ALARM) {
        // ── Alarm ringing screen ──
        if (alarmRinging) {
            // Flashing title
            bool flash = (millis() / 400) % 2;
            spr.setTextColor(flash ? COL_RED : COL_YELLOW, COL_BG);
            spr.drawString("ALARM!", 120, 50, 4);

            // Show alarm time
            char alarmBuf[6];
            int aH = alarmHour % 12;
            if (aH == 0) aH = 12;
            sprintf(alarmBuf, "%d:%02d", aH, alarmMinute);
            spr.setTextColor(COL_WHITE, COL_BG);
            spr.drawString(alarmBuf, 110, 95, 7);
            const char* aAmPm = (alarmHour < 12) ? "AM" : "PM";
            spr.drawString(aAmPm, 190, 80, 4);

            // Animated bell icon (simple oscillating lines)
            int bellX = 120, bellY = 140;
            int swing = ((millis() / 200) % 3) - 1; // -1, 0, 1
            spr.fillCircle(bellX + swing, bellY - 8, 3, COL_YELLOW);
            spr.fillTriangle(bellX - 10 + swing, bellY, bellX + 10 + swing, bellY,
                             bellX + swing, bellY - 14, COL_YELLOW);
            spr.drawFastHLine(bellX - 12 + swing, bellY + 1, 24, COL_YELLOW);
            spr.fillCircle(bellX + swing, bellY + 4, 2, COL_YELLOW);

            // Snooze / Stop buttons
            if (alarmSnoozeMin > 0) {
                spr.setTextColor(COL_GREEN, COL_BG);
                char snoozeBuf[24];
                sprintf(snoozeBuf, "NEXT = Snooze %dm", alarmSnoozeMin);
                spr.drawString(snoozeBuf, 120, 170, 2);
            }
            spr.setTextColor(COL_RED, COL_BG);
            spr.drawString("BACK = Stop", 120, 190, 2);

        // ── Snooze countdown screen ──
        } else if (alarmSnoozing) {
            spr.setTextColor(COL_CYAN, COL_BG);
            spr.drawString("Snoozing", 120, 50, 4);

            // Show remaining snooze time
            unsigned long remaining = 0;
            if (millis() < alarmSnoozeEnd)
                remaining = alarmSnoozeEnd - millis();
            int sMin = remaining / 60000;
            int sSec = (remaining % 60000) / 1000;
            char snoozeBuf[10];
            sprintf(snoozeBuf, "%d:%02d", sMin, sSec);
            spr.setTextColor(COL_WHITE, COL_BG);
            spr.drawString(snoozeBuf, 120, 100, 7);

            spr.setTextColor(COL_MID_GRAY, COL_BG);
            spr.drawString("Alarm will ring again", 120, 150, 2);

            // Show alarm time below
            char alarmBuf[12];
            int aH = alarmHour % 12;
            if (aH == 0) aH = 12;
            sprintf(alarmBuf, "%d:%02d %s", aH, alarmMinute, (alarmHour < 12) ? "AM" : "PM");
            spr.setTextColor(COL_GOLD, COL_BG);
            spr.drawString(alarmBuf, 120, 175, 2);

        // ── Normal alarm display ──
        } else {
            spr.setTextColor(COL_WHITE, COL_BG);
            spr.drawString("Alarm", 120, 55, 4);

            char alarmBuf[6];
            int aH = alarmHour % 12;
            if (aH == 0) aH = 12;
            sprintf(alarmBuf, "%d:%02d", aH, alarmMinute);
            spr.setTextColor(alarmEnabled ? COL_GREEN : COL_MID_GRAY, COL_BG);
            spr.drawString(alarmBuf, 110, 100, 7);

            const char* aAmPm = (alarmHour < 12) ? "AM" : "PM";
            spr.drawString(aAmPm, 190, 85, 4);

            spr.setTextColor(alarmEnabled ? COL_GREEN : COL_RED, COL_BG);
            spr.drawString(alarmEnabled ? "ON" : "OFF", 120, 140, 4);

            // Show snooze setting
            char snoozeBuf[16];
            if (alarmSnoozeMin > 0)
                sprintf(snoozeBuf, "Snooze: %dm", alarmSnoozeMin);
            else
                sprintf(snoozeBuf, "Snooze: Off");
            spr.setTextColor(COL_MID_GRAY, COL_BG);
            spr.drawString(snoozeBuf, 120, 162, 2);

            if (inSettings) {
                spr.setTextColor(COL_YELLOW, COL_BG);
                const char* fields[] = {"< > Hour", "< > Minute", "< > On/Off", "< > Snooze"};
                spr.drawString(fields[settingsField], 120, 182, 2);
                spr.setTextColor(COL_MID_GRAY, COL_BG);
                spr.drawString("NEXT=Adjust BACK=Done", 120, 200, 1);
            } else {
                spr.setTextColor(COL_MID_GRAY, COL_BG);
                spr.drawString("SETTINGS to edit", 120, 185, 2);
            }
        }

    } else if (timerMode == TIMER_STOPWATCH) {
        unsigned long elapsed = stopwatchElapsed;
        if (stopwatchRunning) {
            elapsed += millis() - stopwatchStart;
        }

        int ms  = (elapsed % 1000) / 10;
        int sec = (elapsed / 1000) % 60;
        int min = (elapsed / 60000) % 60;
        int hr  = elapsed / 3600000;

        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString("Stopwatch", 120, 55, 4);

        char swBuf[15];
        if (hr > 0) {
            sprintf(swBuf, "%d:%02d:%02d", hr, min, sec);
        } else {
            sprintf(swBuf, "%02d:%02d", min, sec);
        }
        spr.setTextColor(stopwatchRunning ? COL_GREEN : COL_WHITE, COL_BG);
        spr.drawString(swBuf, 120, 100, 7);

        char msBuf[5];
        sprintf(msBuf, ".%02d", ms);
        spr.setTextColor(COL_CYAN, COL_BG);
        spr.drawString(msBuf, 120, 140, 4);

        spr.setTextColor(COL_MID_GRAY, COL_BG);
        spr.drawString("SETTINGS=Start/Stop", 120, 175, 2);
        spr.drawString("Hold SETTINGS=Reset", 120, 195, 2);
    }

    spr.pushSprite(0, 0);
}

// ── Screen 4: Pomodoro Timer ───────────────────────────────────
void drawPomodoroScreen() {
    spr.fillSprite(COL_BG);
    drawCircleBorder();
    drawScreenIndicator(3);

    spr.setTextDatum(MC_DATUM);

    if (pomoInSettings) {
        // ── Settings screen ──
        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString("Pomodoro Setup", 120, 35, 2);

        const char* labels[] = {"Work (min)", "Break (min)"};
        int values[] = {pomoWorkMin, pomoBreakMin};

        for (int i = 0; i < 2; i++) {
            int y = 75 + i * 70;
            bool selected = (pomoField == i);
            bool editing = selected && pomoEditing;

            spr.setTextColor(selected ? COL_CYAN : COL_MID_GRAY, COL_BG);
            spr.drawString(labels[i], 120, y, 2);

            char valBuf[5];
            sprintf(valBuf, "%d", values[i]);
            if (editing) {
                spr.setTextColor(COL_GREEN, COL_BG);
                spr.drawString("<", 70, y + 28, 4);
                spr.drawString(">", 170, y + 28, 4);
            }
            spr.setTextColor(editing ? COL_GREEN : (selected ? COL_WHITE : COL_MID_GRAY), COL_BG);
            spr.drawString(valBuf, 120, y + 28, 4);
        }

        spr.setTextColor(COL_MID_GRAY, COL_BG);
        if (pomoEditing) {
            spr.drawString("BACK=- NEXT=+ SET=Confirm", 120, 205, 1);
        } else {
            spr.drawString("NEXT=Field SET=Edit BACK=Done", 120, 205, 1);
        }

        spr.pushSprite(0, 0);
        return;
    }

    // ── Main Pomodoro display ──
    // Title with state
    const char* stateLabel;
    uint16_t stateCol;
    if (pomoState == POMO_WORK) {
        stateLabel = "WORK";
        stateCol = COL_RED;
    } else if (pomoState == POMO_BREAK) {
        stateLabel = "BREAK";
        stateCol = COL_GREEN;
    } else {
        stateLabel = "Pomodoro";
        stateCol = COL_WHITE;
    }
    spr.setTextColor(stateCol, COL_BG);
    spr.drawString(stateLabel, 120, 40, 4);

    // Countdown
    unsigned long displayMs = pomoTimeLeft;
    if (pomoState == POMO_IDLE) {
        displayMs = (unsigned long)pomoWorkMin * 60000UL;
    }
    int totalSec = displayMs / 1000;
    int dispMin = totalSec / 60;
    int dispSec = totalSec % 60;

    char timeBuf[6];
    sprintf(timeBuf, "%02d:%02d", dispMin, dispSec);
    spr.setTextColor(pomoRunning ? stateCol : COL_WHITE, COL_BG);
    spr.drawString(timeBuf, 120, 90, 7);

    // Progress arc
    if (pomoState != POMO_IDLE) {
        unsigned long totalMs = (pomoState == POMO_WORK)
            ? (unsigned long)pomoWorkMin * 60000UL
            : (unsigned long)pomoBreakMin * 60000UL;
        float progress = 1.0f - (float)pomoTimeLeft / (float)totalMs;
        int dots = (int)(progress * 60);
        uint16_t arcCol = (pomoState == POMO_WORK) ? COL_RED : COL_GREEN;
        for (int s = 0; s < dots; s++) {
            float a = s * 6.0 - 90.0;
            float rad = a * DEG_TO_RAD;
            int px = 120 + (int)(105 * cos(rad));
            int py = 120 + (int)(105 * sin(rad));
            spr.fillCircle(px, py, 2, arcCol);
        }
    }

    // Status text
    spr.setTextColor(COL_MID_GRAY, COL_BG);
    if (pomoState == POMO_IDLE) {
        char infoBuf[20];
        sprintf(infoBuf, "%dm work / %dm break", pomoWorkMin, pomoBreakMin);
        spr.drawString(infoBuf, 120, 130, 2);
        spr.drawString("NEXT=Start SET=Config", 120, 175, 1);
        spr.drawString("BACK=Reset  Hold BACK=Exit", 120, 190, 1);
    } else {
        spr.drawString(pomoRunning ? "Running" : "Paused", 120, 130, 2);
        spr.drawString("NEXT=Start/Stop", 120, 175, 1);
        spr.drawString("BACK=Reset  Hold BACK=Exit", 120, 190, 1);
    }

    spr.pushSprite(0, 0);
}

void updatePomodoro() {
    if (!pomoRunning || pomoState == POMO_IDLE) return;

    unsigned long now = millis();
    unsigned long delta = now - pomoLastTick;
    pomoLastTick = now;

    if (delta >= pomoTimeLeft) {
        pomoTimeLeft = 0;
        pomoRunning = false;

        if (pomoState == POMO_WORK) {
            // Work done — switch to break
            pomoState = POMO_BREAK;
            pomoTimeLeft = (unsigned long)pomoBreakMin * 60000UL;
            pomoLastTick = millis();
            pomoRunning = true;
            chimeBreakStart();
        } else {
            // Break done — back to idle
            pomoState = POMO_IDLE;
            chimeBreakEnd();
        }
    } else {
        pomoTimeLeft -= delta;
    }
}

// ── Geometry Dash: Helpers ──────────────────────────────────────
void gameUpdateTheme() {
    // Color theme shifts every 10 points — player, accent, ground, background
    int tier = gameScore / 10;
    switch (tier % 6) {
        case 0: gamePlayerCol = COL_GREEN;   gameAccentCol = COL_CYAN;       gameGroundCol = COL_DARK_GRAY; gameBgCol = 0x0000; break;
        case 1: gamePlayerCol = COL_CYAN;    gameAccentCol = COL_MAGENTA;    gameGroundCol = 0x2104; gameBgCol = 0x0821; break; // dark blue tint
        case 2: gamePlayerCol = COL_YELLOW;  gameAccentCol = COL_ORANGE;     gameGroundCol = 0x1082; gameBgCol = 0x1000; break; // dark red tint
        case 3: gamePlayerCol = COL_MAGENTA; gameAccentCol = COL_LIGHT_BLUE; gameGroundCol = 0x0811; gameBgCol = 0x0811; break; // dark teal tint
        case 4: gamePlayerCol = COL_ORANGE;  gameAccentCol = COL_GREEN;      gameGroundCol = 0x2945; gameBgCol = 0x0841; break; // dark purple tint
        case 5: gamePlayerCol = COL_GOLD;    gameAccentCol = COL_RED;        gameGroundCol = 0x18C3; gameBgCol = 0x1020; break; // dark green tint
    }
}

void gameSpawnParticle(float x, float y, float vx, float vy, uint16_t col, int life) {
    for (int i = 0; i < GAME_MAX_PARTS; i++) {
        if (!gameParts[i].active) {
            gameParts[i] = {x, y, vx, vy, col, life, true};
            return;
        }
    }
}

void gameInitStars() {
    for (int i = 0; i < GAME_MAX_STARS; i++) {
        gameStars[i].x = random(240);
        gameStars[i].y = 20 + random(GAME_GROUND_Y - 40);
        uint8_t b = 4 + random(12);
        gameStars[i].col = (b << 11) | (b << 6) | b; // dim white-ish
        gameStars[i].speed = 0.15f + (random(100) / 200.0f);
    }
}

void gameInitBgCols() {
    // Dark silhouette columns at varying depths
    for (int i = 0; i < GAME_MAX_COLS; i++) {
        gameBgCols[i].x = random(280);
        gameBgCols[i].w = 12 + random(20);
        gameBgCols[i].h = 25 + random(50);
        gameBgCols[i].speed = 0.12f + (random(100) / 400.0f); // 0.12 - 0.37
        // Darker columns = farther, brighter = closer
        uint8_t v = 1 + (int)(gameBgCols[i].speed * 8);
        gameBgCols[i].col = (v << 11) | (v << 6) | v;
    }
}

// ── Geometry Dash: Game Logic ───────────────────────────────────
void gameReset() {
    gamePlayerY = GAME_GROUND_Y - GAME_PLAYER_SZ * 2;
    gameVelY = 0;
    gameOnGround = true;
    gameCanDouble = false;
    gamePlayerRot = 0;
    gameSquash = 1.0f;
    gameScore = 0;
    gameSpeed = 2.8f;
    gameSpawnDist = 140;
    gameFlashTimer = 0;
    for (int i = 0; i < GAME_MAX_OBS; i++) gameObs[i].active = false;
    for (int i = 0; i < GAME_MAX_PARTS; i++) gameParts[i].active = false;
    gameInitStars();
    gameInitBgCols();
    gameBgScroll = 0;
    gameUpdateTheme();
    gameFrameMs = millis();
}

void gameSpawnObstacle() {
    for (int i = 0; i < GAME_MAX_OBS; i++) {
        if (!gameObs[i].active) {
            gameObs[i].x = 245;
            // Type probabilities shift with score
            int r = random(100);
            if (gameScore < 5) {
                gameObs[i].type = 0; // only singles early on
            } else if (gameScore < 15) {
                gameObs[i].type = (r < 70) ? 0 : 1;
            } else {
                gameObs[i].type = (r < 40) ? 0 : (r < 75) ? 1 : 2;
            }
            switch (gameObs[i].type) {
                case 0: // single spike
                    gameObs[i].halfW = 7 + random(4);
                    gameObs[i].h = 20 + random(8);
                    break;
                case 1: // double spike (wider)
                    gameObs[i].halfW = 14 + random(4);
                    gameObs[i].h = 18 + random(6);
                    break;
                case 2: // tall spike
                    gameObs[i].halfW = 6 + random(3);
                    gameObs[i].h = 30 + random(8);
                    break;
            }
            gameObs[i].active = true;
            gameObs[i].scored = false;
            return;
        }
    }
}

void gameDeathExplosion() {
    float px = GAME_PLAYER_X;
    float py = gamePlayerY + GAME_PLAYER_SZ;
    uint16_t cols[] = {gamePlayerCol, gameAccentCol, COL_WHITE, COL_YELLOW, COL_RED};
    for (int i = 0; i < 15; i++) {
        float angle = (random(360)) * 0.01745f;
        float spd = 1.5f + random(100) / 30.0f;
        gameSpawnParticle(px, py, cosf(angle) * spd, sinf(angle) * spd,
                          cols[random(5)], 12 + random(10));
    }
}

void updateGame() {
    if (gameState != GAME_PLAYING) {
        // Still update death particles in GAME_OVER
        if (gameState == GAME_OVER) {
            for (int i = 0; i < GAME_MAX_PARTS; i++) {
                if (!gameParts[i].active) continue;
                gameParts[i].x += gameParts[i].vx;
                gameParts[i].y += gameParts[i].vy;
                gameParts[i].vy += 0.3f;
                if (--gameParts[i].life <= 0) gameParts[i].active = false;
            }
        }
        return;
    }

    // Physics: gravity + position
    gameVelY += GAME_GRAVITY;
    gamePlayerY += gameVelY;
    float groundPos = GAME_GROUND_Y - GAME_PLAYER_SZ * 2;
    if (gamePlayerY >= groundPos) {
        gamePlayerY = groundPos;
        if (!gameOnGround) {
            gameLandMs = millis();
            gameSquash = 0.7f; // squash on landing
        }
        gameVelY = 0;
        gameOnGround = true;
        gameCanDouble = false;
        gamePlayerRot = 0; // snap upright on ground
    }

    // Rotation while airborne
    if (!gameOnGround) {
        gamePlayerRot += 0.18f;
    }

    // Squash recovery
    if (gameSquash < 1.0f) {
        gameSquash += 0.06f;
        if (gameSquash > 1.0f) gameSquash = 1.0f;
    }

    // Trail particles while jumping
    if (!gameOnGround && random(3) == 0) {
        gameSpawnParticle(
            GAME_PLAYER_X - GAME_PLAYER_SZ + random(4),
            gamePlayerY + GAME_PLAYER_SZ * 2,
            -0.5f - random(100) / 100.0f, 0.5f + random(100) / 100.0f,
            gameAccentCol, 6 + random(4));
    }

    // Update particles
    for (int i = 0; i < GAME_MAX_PARTS; i++) {
        if (!gameParts[i].active) continue;
        gameParts[i].x += gameParts[i].vx;
        gameParts[i].y += gameParts[i].vy;
        gameParts[i].vy += 0.15f;
        if (--gameParts[i].life <= 0) gameParts[i].active = false;
    }

    // Update background stars
    for (int i = 0; i < GAME_MAX_STARS; i++) {
        gameStars[i].x -= gameSpeed * gameStars[i].speed;
        if (gameStars[i].x < -2) {
            gameStars[i].x = 242;
            gameStars[i].y = 20 + random(GAME_GROUND_Y - 40);
        }
    }

    // Update background columns
    for (int i = 0; i < GAME_MAX_COLS; i++) {
        gameBgCols[i].x -= gameSpeed * gameBgCols[i].speed;
        if (gameBgCols[i].x + gameBgCols[i].w < -5) {
            gameBgCols[i].x = 245 + random(40);
            gameBgCols[i].w = 12 + random(20);
            gameBgCols[i].h = 25 + random(50);
        }
    }

    // Accumulate ground scroll for perspective lines
    gameBgScroll += gameSpeed;

    // Move obstacles left
    for (int i = 0; i < GAME_MAX_OBS; i++) {
        if (!gameObs[i].active) continue;
        gameObs[i].x -= gameSpeed;
        if (gameObs[i].x < -30) {
            gameObs[i].active = false;
            continue;
        }
        // Score when obstacle passes player
        if (!gameObs[i].scored && gameObs[i].x < GAME_PLAYER_X - GAME_PLAYER_SZ) {
            gameObs[i].scored = true;
            gameScore++;
            // Score particle burst
            gameSpawnParticle(GAME_PLAYER_X + 20, gamePlayerY,
                              1.5f, -1.5f, COL_GOLD, 10);
            // Speed up every 5 points (gentler curve)
            if (gameScore % 5 == 0) {
                gameSpeed += 0.3f;
                if (gameSpeed > 7.0f) gameSpeed = 7.0f; // cap
            }
            // Milestone flash every 10 points
            if (gameScore % 10 == 0) {
                gameFlashTimer = 8;
                gameFlashCol = gameAccentCol;
                gameUpdateTheme();
            }
        }
    }

    // Flash countdown
    if (gameFlashTimer > 0) gameFlashTimer--;

    // Spawn new obstacles
    gameSpawnDist -= gameSpeed;
    if (gameSpawnDist <= 0) {
        gameSpawnObstacle();
        // Gap scales with speed to stay fair — wider gaps at higher speeds
        float minGap = 55 + gameSpeed * 4;
        float maxGap = minGap + 40;
        gameSpawnDist = minGap + random((int)maxGap - (int)minGap);
    }

    // Collision detection: player square vs triangle obstacles
    int sz = GAME_PLAYER_SZ;
    // Shrink hitbox slightly for fairness (4px inset on each side)
    float pLeft   = GAME_PLAYER_X - sz + 4;
    float pRight  = GAME_PLAYER_X + sz - 4;
    float pTop    = gamePlayerY + 4;
    float pBottom = gamePlayerY + sz * 2 - 2;

    for (int i = 0; i < GAME_MAX_OBS; i++) {
        if (!gameObs[i].active) continue;
        float ox = gameObs[i].x;
        int hw = gameObs[i].halfW;
        int oh = gameObs[i].h;

        if (gameObs[i].type == 1) {
            // Double spike: two triangles side by side
            // Check each spike: left spike at ox - hw/2, right spike at ox + hw/2
            for (int s = -1; s <= 1; s += 2) {
                float sx = ox + s * (hw / 2.0f);
                int shw = hw / 3;
                float tLeft = sx - shw, tRight = sx + shw;
                float tTop = GAME_GROUND_Y - oh;
                if (pRight <= tLeft || pLeft >= tRight || pBottom <= tTop) continue;
                float overlapMid = (max(pLeft, tLeft) + min(pRight, tRight)) / 2.0f;
                float distEdge = min(overlapMid - tLeft, tRight - overlapMid);
                float triTopAtX = GAME_GROUND_Y - oh * distEdge / (float)shw;
                if (pBottom > triTopAtX + 4) {
                    gameState = GAME_OVER;
                    if (gameScore > gameHiScore) gameHiScore = gameScore;
                    gameDeathExplosion();
                    chimeDeath();
                    return;
                }
            }
        } else {
            // Single or tall spike
            float tLeft  = ox - hw;
            float tRight = ox + hw;
            float tTop   = GAME_GROUND_Y - oh;
            if (pRight <= tLeft || pLeft >= tRight || pBottom <= tTop) continue;
            float overlapMid = (max(pLeft, tLeft) + min(pRight, tRight)) / 2.0f;
            float distEdge = min(overlapMid - tLeft, tRight - overlapMid);
            float triTopAtX = GAME_GROUND_Y - oh * distEdge / (float)hw;
            if (pBottom > triTopAtX + 4) {
                gameState = GAME_OVER;
                if (gameScore > gameHiScore) gameHiScore = gameScore;
                gameDeathExplosion();
                chimeDeath();
                return;
            }
        }
    }
}

// ── Geometry Dash: Drawing ─────────────────────────────────────
void drawGameRotatedSquare(int cx, int cy, int sz, float angle, uint16_t fillCol, uint16_t outCol) {
    // Draw a rotated square using 4 corner points
    float c = cosf(angle), s = sinf(angle);
    int px[4], py[4];
    float offsets[4][2] = {{-1,-1},{1,-1},{1,1},{-1,1}};
    for (int i = 0; i < 4; i++) {
        float ox = offsets[i][0] * sz;
        float oy = offsets[i][1] * sz;
        px[i] = cx + (int)(ox * c - oy * s);
        py[i] = cy + (int)(ox * s + oy * c);
    }
    // Fill with two triangles
    spr.fillTriangle(px[0], py[0], px[1], py[1], px[2], py[2], fillCol);
    spr.fillTriangle(px[0], py[0], px[2], py[2], px[3], py[3], fillCol);
    // Outline
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        spr.drawLine(px[i], py[i], px[j], py[j], outCol);
    }
}

void drawGameScreen() {
    spr.fillSprite(COL_BG);
    drawCircleBorder();

    if (gameState == GAME_READY) {
        // Animated background stars on title
        static float titleStarX[8];
        static bool titleInit = false;
        if (!titleInit) {
            for (int i = 0; i < 8; i++) titleStarX[i] = random(240);
            titleInit = true;
        }
        for (int i = 0; i < 8; i++) {
            int sy = 30 + i * 22;
            titleStarX[i] -= 0.5f;
            if (titleStarX[i] < 0) titleStarX[i] = 240;
            spr.drawPixel((int)titleStarX[i], sy, COL_DARK_GRAY);
            spr.drawPixel((int)titleStarX[i] + 1, sy, COL_MID_GRAY);
        }

        // Title with color cycling
        bool flash = (millis() / 600) % 2;
        spr.setTextColor(flash ? COL_CYAN : COL_MAGENTA, COL_BG);
        spr.drawString("GEOMETRY", 120, 52, 4);
        spr.setTextColor(flash ? COL_MAGENTA : COL_CYAN, COL_BG);
        spr.drawString("DASH", 120, 80, 4);

        // Animated player + obstacle scene
        int animY = 135;
        // Ground
        spr.drawFastHLine(40, animY + 14, 160, COL_DARK_GRAY);
        // Bouncing player
        int bounce = abs((int)((millis() / 150) % 12) - 6);
        spr.fillRect(85, animY - bounce, 14, 14, COL_GREEN);
        spr.drawRect(85, animY - bounce, 14, 14, COL_WHITE);
        // Trail
        for (int t = 0; t < 3; t++) {
            spr.fillRect(75 - t * 12, animY + 2 + t, 6 - t, 6 - t, COL_CYAN);
        }
        // Triangles
        uint16_t triCols[] = {COL_RED, COL_ORANGE, COL_MAGENTA};
        int triX[] = {120, 145, 165};
        int triH[] = {14, 18, 12};
        for (int t = 0; t < 3; t++) {
            spr.fillTriangle(triX[t], animY + 14 - triH[t],
                             triX[t] - 5, animY + 14,
                             triX[t] + 5, animY + 14, triCols[t]);
        }

        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString("Press SET to play", 120, 168, 2);

        spr.setTextColor(COL_MID_GRAY, COL_BG);
        spr.drawString("Double-tap to double jump!", 120, 186, 1);

        if (gameHiScore > 0) {
            spr.setTextColor(COL_GOLD, COL_BG);
            char buf[20];
            sprintf(buf, "Best: %d", gameHiScore);
            spr.drawString(buf, 120, 208, 2);
        }

        drawScreenIndicator(SCREEN_GAME);
        spr.pushSprite(0, 0);
        return;
    }

    if (gameState == GAME_CONFIRM_EXIT) {
        // Dim background with border
        spr.drawCircle(120, 120, 80, COL_DARK_GRAY);

        spr.setTextColor(COL_YELLOW, COL_BG);
        spr.drawString("PAUSED", 120, 65, 4);

        spr.setTextColor(COL_WHITE, COL_BG);
        char buf[20];
        sprintf(buf, "Score: %d", gameScore);
        spr.drawString(buf, 120, 105, 4);

        // Speed indicator
        spr.setTextColor(COL_MID_GRAY, COL_BG);
        char speedBuf[16];
        sprintf(speedBuf, "Speed: %.1f", gameSpeed);
        spr.drawString(speedBuf, 120, 132, 2);

        spr.setTextColor(COL_GREEN, COL_BG);
        spr.drawString("SET = Resume", 120, 162, 2);
        spr.setTextColor(COL_RED, COL_BG);
        spr.drawString("BACK = Quit", 120, 182, 2);

        spr.pushSprite(0, 0);
        return;
    }

    if (gameState == GAME_OVER) {
        // Death particles still render
        for (int i = 0; i < GAME_MAX_PARTS; i++) {
            if (!gameParts[i].active) continue;
            int px = (int)gameParts[i].x, py = (int)gameParts[i].y;
            if (px > 5 && px < 235 && py > 5 && py < 235) {
                int r = (gameParts[i].life > 8) ? 3 : (gameParts[i].life > 4) ? 2 : 1;
                spr.fillCircle(px, py, r, gameParts[i].col);
            }
        }

        bool isNewHi = (gameScore == gameHiScore && gameScore > 0);
        bool flash = (millis() / 300) % 2;

        if (isNewHi) {
            spr.setTextColor(flash ? COL_GOLD : COL_YELLOW, COL_BG);
            spr.drawString("NEW BEST!", 120, 45, 4);
        }

        spr.setTextColor(COL_RED, COL_BG);
        spr.drawString("GAME OVER", 120, 70, 4);

        spr.setTextColor(COL_WHITE, COL_BG);
        char buf[32];
        sprintf(buf, "%d", gameScore);
        spr.drawString(buf, 120, 108, 7);

        if (!isNewHi && gameHiScore > 0) {
            spr.setTextColor(COL_GOLD, COL_BG);
            sprintf(buf, "Best: %d", gameHiScore);
            spr.drawString(buf, 120, 148, 2);
        }

        spr.setTextColor(COL_CYAN, COL_BG);
        spr.drawString("SET = Retry", 120, 178, 2);
        spr.setTextColor(COL_MID_GRAY, COL_BG);
        spr.drawString("BACK = Exit", 120, 196, 2);

        spr.pushSprite(0, 0);
        return;
    }

    // ── GAME_PLAYING: render the game ──

    // Tinted background fill
    if (gameBgCol != 0x0000) {
        // Fill play area with subtle tint (inside circle)
        for (int y = 20; y < GAME_GROUND_Y; y += 4) {
            // Approximate circle clipping
            int dy = abs(y - 120);
            if (dy > 116) continue;
            int hw = (int)sqrtf(116 * 116 - dy * dy);
            int x0 = max(120 - hw, 4);
            int x1 = min(120 + hw, 236);
            spr.drawFastHLine(x0, y, x1 - x0, gameBgCol);
        }
    }

    // Background silhouette columns (farthest layer — city/mountains)
    for (int i = 0; i < GAME_MAX_COLS; i++) {
        int bx = (int)gameBgCols[i].x;
        int bw = gameBgCols[i].w;
        int bh = gameBgCols[i].h;
        // Draw from ground upward
        int by = GAME_GROUND_Y - bh;
        if (bx + bw < 10 || bx > 230) continue;
        // Clip to circle roughly
        spr.fillRect(max(bx, 10), max(by, 20), min(bw, 230 - max(bx, 10)),
                      GAME_GROUND_Y - max(by, 20), gameBgCols[i].col);
        // Top cap (slight trapezoid look — 2px narrower on top)
        spr.fillRect(max(bx + 1, 10), max(by - 2, 20),
                      max(min(bw - 2, 230 - max(bx + 1, 10)), 0), 2, gameBgCols[i].col);
    }

    // Background stars (parallax) — streaks at high speed
    for (int i = 0; i < GAME_MAX_STARS; i++) {
        int sx = (int)gameStars[i].x, sy = (int)gameStars[i].y;
        if (sx > 5 && sx < 235 && sy > 15 && sy < GAME_GROUND_Y - 5) {
            if (gameSpeed > 4.5f) {
                // Draw as speed streak
                int streakLen = (int)(gameSpeed * gameStars[i].speed * 3);
                spr.drawFastHLine(sx, sy, min(streakLen, 235 - sx), gameStars[i].col);
            } else {
                spr.drawPixel(sx, sy, gameStars[i].col);
            }
        }
    }

    // Milestone flash overlay
    if (gameFlashTimer > 0) {
        spr.drawCircle(120, 120, 100 - gameFlashTimer * 5, gameFlashCol);
        spr.drawCircle(120, 120, 101 - gameFlashTimer * 5, gameFlashCol);
    }

    // Ground: solid line + texture
    spr.drawFastHLine(15, GAME_GROUND_Y, 210, gameGroundCol);
    spr.drawFastHLine(15, GAME_GROUND_Y + 1, 210, gameGroundCol);

    // Perspective ground lines — converge to vanishing point at horizon
    int vpX = 120, vpY = GAME_GROUND_Y - 80; // vanishing point
    int scrollOff = ((int)gameBgScroll) % 30;
    for (int gx = -30 - scrollOff; gx < 260; gx += 30) {
        // Lines from ground edge to vanishing point — faded
        int fromX = gx;
        int fromY = GAME_GROUND_Y + 12;
        // Only draw the bottom portion (from ground down)
        if (fromX > 10 && fromX < 230) {
            spr.drawLine(fromX, fromY, vpX, vpY, gameGroundCol);
        }
    }

    // Horizontal depth lines below ground (scrolling perspective floor)
    int depthLines[] = {3, 7, 13, 21};
    for (int d = 0; d < 4; d++) {
        int ly = GAME_GROUND_Y + depthLines[d];
        if (ly > 230) continue;
        // Each line is dimmer as it goes down
        uint16_t lCol = gameGroundCol;
        int lx0 = 15 + d * 8;
        int lx1 = 225 - d * 8;
        spr.drawFastHLine(lx0, ly, lx1 - lx0, lCol);
    }

    // Particles (behind player)
    for (int i = 0; i < GAME_MAX_PARTS; i++) {
        if (!gameParts[i].active) continue;
        int px = (int)gameParts[i].x, py = (int)gameParts[i].y;
        if (px > 5 && px < 235 && py > 5 && py < 235) {
            if (gameParts[i].life > 5)
                spr.fillCircle(px, py, 2, gameParts[i].col);
            else
                spr.drawPixel(px, py, gameParts[i].col);
        }
    }

    // Draw obstacles
    for (int i = 0; i < GAME_MAX_OBS; i++) {
        if (!gameObs[i].active) continue;
        int ox = (int)gameObs[i].x;
        int hw = gameObs[i].halfW;
        int oh = gameObs[i].h;

        // Color based on type
        uint16_t obsCol, obsHighlight;
        switch (gameObs[i].type) {
            case 0: obsCol = COL_RED;     obsHighlight = COL_ORANGE;  break;
            case 1: obsCol = COL_MAGENTA; obsHighlight = COL_LIGHT_BLUE; break;
            case 2: obsCol = COL_ORANGE;  obsHighlight = COL_YELLOW;  break;
            default: obsCol = COL_RED;    obsHighlight = COL_ORANGE;  break;
        }

        if (gameObs[i].type == 1) {
            // Double spike: two triangles
            int gap = hw / 2;
            int shw = hw / 3;
            for (int s = -1; s <= 1; s += 2) {
                int sx = ox + s * gap;
                spr.fillTriangle(sx, GAME_GROUND_Y - oh,
                                 sx - shw, GAME_GROUND_Y,
                                 sx + shw, GAME_GROUND_Y, obsCol);
                spr.drawLine(sx, GAME_GROUND_Y - oh,
                             sx - shw, GAME_GROUND_Y, obsHighlight);
                // Inner highlight
                spr.drawLine(sx, GAME_GROUND_Y - oh,
                             sx + shw, GAME_GROUND_Y, obsCol);
            }
        } else {
            // Single or tall spike
            spr.fillTriangle(ox, GAME_GROUND_Y - oh,
                             ox - hw, GAME_GROUND_Y,
                             ox + hw, GAME_GROUND_Y, obsCol);
            // Left highlight edge
            spr.drawLine(ox, GAME_GROUND_Y - oh,
                         ox - hw, GAME_GROUND_Y, obsHighlight);
            // Right darker edge
            spr.drawLine(ox, GAME_GROUND_Y - oh,
                         ox + hw, GAME_GROUND_Y, obsCol);
            // Inner glow line
            if (oh > 22) {
                spr.drawLine(ox, GAME_GROUND_Y - oh + 3,
                             ox - hw + 3, GAME_GROUND_Y, obsHighlight);
            }
        }
    }

    // Draw player
    int py = (int)gamePlayerY;
    int sz = GAME_PLAYER_SZ;
    int cx = GAME_PLAYER_X;
    int cy = py + sz; // center of square

    // Apply squash/stretch
    int drawW = (int)(sz * (2.0f - gameSquash));
    int drawH = (int)(sz * gameSquash);

    if (!gameOnGround) {
        // Rotated square while airborne
        drawGameRotatedSquare(cx, cy, sz, gamePlayerRot, gamePlayerCol, COL_WHITE);
        // Eye (relative to rotation)
        float ec = cosf(gamePlayerRot), es = sinf(gamePlayerRot);
        int ex = cx + (int)(4 * ec - (-4) * es);
        int ey = cy + (int)(4 * es + (-4) * ec);
        spr.fillCircle(ex, ey, 3, COL_BG);
        spr.fillCircle(ex + 1, ey, 2, COL_WHITE);
        // Double-jump indicator
        if (gameCanDouble) {
            spr.drawCircle(cx, cy, sz + 4, gameAccentCol);
        }
    } else {
        // Ground: squash/stretch rectangle
        int rx = cx - drawW;
        int ry = cy - drawH + (sz - drawH); // keep bottom aligned
        spr.fillRect(rx, ry, drawW * 2, drawH * 2, gamePlayerCol);
        spr.drawRect(rx, ry, drawW * 2, drawH * 2, COL_WHITE);
        // Eye
        spr.fillRect(cx + 2, ry + 3, 5, 5, COL_BG);
        spr.fillRect(cx + 3, ry + 4, 3, 3, COL_WHITE);
    }

    // Score display at top
    if (gameFlashTimer > 0 && gameFlashTimer % 2 == 0) {
        spr.setTextColor(gameFlashCol, COL_BG);
    } else {
        spr.setTextColor(COL_WHITE, COL_BG);
    }
    char scoreBuf[16];
    sprintf(scoreBuf, "%d", gameScore);
    spr.drawString(scoreBuf, 120, 22, 4);

    // Speed bar at top-right
    int barW = min((int)(gameSpeed * 5), 40);
    uint16_t barCol = (gameSpeed < 4) ? COL_GREEN : (gameSpeed < 5.5f) ? COL_YELLOW : COL_RED;
    spr.fillRect(195 - barW, 12, barW, 4, barCol);
    spr.drawRect(155, 12, 40, 4, COL_DARK_GRAY);

    spr.pushSprite(0, 0);
}

// ── Handle Button Actions ───────────────────────────────────────
void handleButtons() {
    // ── Alarm ringing — global intercept ──
    if (alarmRinging) {
        if (buttons[0].pressed) {   // BACK — stop alarm
            chimeBack();
            alarmStop();
            // Stay on alarm screen so user sees it stopped
        }
        if (buttons[2].pressed) {   // NEXT — snooze (if enabled)
            if (alarmSnoozeMin > 0) {
                chimeNext();
                alarmSnooze();
            }
            // If snooze disabled, NEXT does nothing — must press BACK to stop
        }
        return; // Block all other button handling while alarm rings
    }

    // ── Clock settings (manual time/date when no WiFi) ──
    if (clockInSettings) {
        if (buttons[1].pressed) {  // SETTINGS — cycle field
            chimeSettings();
            clockField = (clockField + 1) % 5;
        }
        if (buttons[2].pressed) {  // NEXT — increment
            chimeNext();
            switch (clockField) {
                case 0: clockSetHour = (clockSetHour + 1) % 24; break;
                case 1: clockSetMin = (clockSetMin + 1) % 60; break;
                case 2: clockSetMonth = (clockSetMonth % 12) + 1; break;
                case 3:
                    clockSetDay = (clockSetDay % daysInMonth(clockSetMonth, clockSetYear)) + 1;
                    break;
                case 4: clockSetYear++; break;
            }
        }
        if (buttons[0].pressed) {  // BACK — exit and apply
            chimeBack();
            applyClockSettings();
            clockInSettings = false;
        }
        return;
    }

    // ── Pomodoro screen buttons ──
    if (currentScreen == SCREEN_POMODORO) {
        if (pomoInSettings) {
            // Settings mode
            if (buttons[2].pressed) {  // NEXT
                chimeNext();
                if (pomoEditing) {
                    // Increment value
                    if (pomoField == 0) pomoWorkMin = min(pomoWorkMin + 1, 99);
                    else                pomoBreakMin = min(pomoBreakMin + 1, 99);
                } else {
                    // Toggle to next field
                    pomoField = (pomoField + 1) % 2;
                }
            }
            if (buttons[0].pressed) {  // BACK
                chimeBack();
                if (pomoEditing) {
                    // Decrement value
                    if (pomoField == 0) pomoWorkMin = max(pomoWorkMin - 1, 1);
                    else                pomoBreakMin = max(pomoBreakMin - 1, 1);
                } else {
                    // Exit settings
                    pomoInSettings = false;
                }
            }
            if (buttons[1].pressed) {  // SETTINGS
                chimeSettings();
                if (pomoEditing) {
                    pomoEditing = false; // Confirm edit
                } else {
                    pomoEditing = true;  // Enter edit mode
                }
            }
            return;
        }

        // Normal pomodoro mode
        if (buttons[2].pressed) {  // NEXT — start/stop
            chimeNext();
            if (pomoState == POMO_IDLE) {
                // Start work session
                pomoState = POMO_WORK;
                pomoTimeLeft = (unsigned long)pomoWorkMin * 60000UL;
                pomoLastTick = millis();
                pomoRunning = true;
            } else {
                // Toggle pause/resume
                if (pomoRunning) {
                    pomoRunning = false;
                } else {
                    pomoLastTick = millis();
                    pomoRunning = true;
                }
            }
        }
        if (buttons[0].pressed) {  // BACK — reset
            chimeBack();
            pomoState = POMO_IDLE;
            pomoRunning = false;
            pomoTimeLeft = 0;
        }
        if (buttons[1].pressed) {  // SETTINGS — enter settings (only when idle)
            if (pomoState == POMO_IDLE) {
                chimeSettings();
                pomoInSettings = true;
                pomoField = 0;
                pomoEditing = false;
            }
        }
        // Long-press NEXT — navigate to next screen
        if (buttons[2].holding && !buttons[2].holdFired &&
            (millis() - buttons[2].holdStart > HOLD_MS)) {
            buttons[2].holdFired = true;
            chimeNext();
            currentScreen = (Screen)((currentScreen + 1) % SCREEN_COUNT);
        }
        // Long-press BACK — exit to watch face
        if (buttons[0].holding && !buttons[0].holdFired &&
            (millis() - buttons[0].holdStart > HOLD_MS)) {
            buttons[0].holdFired = true;
            pomoState = POMO_IDLE;
            pomoRunning = false;
            pomoTimeLeft = 0;
            currentScreen = SCREEN_CLOCK;
            chimeBack();
        }
        return;
    }

    // ── Game screen buttons ──
    if (currentScreen == SCREEN_GAME) {
        if (gameState == GAME_READY) {
            if (buttons[1].pressed) {   // SETTINGS — start game
                chimeSettings();
                gameReset();
                gameState = GAME_PLAYING;
            }
            if (buttons[2].pressed) {   // NEXT — next screen
                chimeNext();
                currentScreen = (Screen)((currentScreen + 1) % SCREEN_COUNT);
            }
            if (buttons[0].pressed) {   // BACK — previous screen
                chimeBack();
                currentScreen = (Screen)((currentScreen - 1 + SCREEN_COUNT) % SCREEN_COUNT);
            }
            return;
        }
        if (gameState == GAME_PLAYING) {
            if (buttons[2].pressed) {   // NEXT — jump / double-jump
                if (gameOnGround) {
                    chimeJump();
                    gameVelY = GAME_JUMP_VEL;
                    gameOnGround = false;
                    gameCanDouble = true;
                    // Jump particle burst
                    for (int p = 0; p < 4; p++) {
                        gameSpawnParticle(
                            GAME_PLAYER_X - 6 + random(12),
                            GAME_GROUND_Y,
                            -1.0f + random(200) / 100.0f, -1.0f - random(100) / 100.0f,
                            gameGroundCol + 0x2104, 5 + random(3));
                    }
                } else if (gameCanDouble) {
                    chimeJump();
                    gameVelY = GAME_JUMP_VEL * 0.85f; // slightly weaker
                    gameCanDouble = false;
                    gamePlayerRot += 0.5f; // spin boost
                    // Double-jump sparkle
                    for (int p = 0; p < 3; p++) {
                        gameSpawnParticle(
                            GAME_PLAYER_X, gamePlayerY + GAME_PLAYER_SZ,
                            -1.5f + random(300) / 100.0f, 1.0f + random(100) / 100.0f,
                            gameAccentCol, 6);
                    }
                }
            }
            if (buttons[0].pressed) {   // BACK — confirm exit
                gameState = GAME_CONFIRM_EXIT;
            }
            return;
        }
        if (gameState == GAME_CONFIRM_EXIT) {
            if (buttons[1].pressed) {   // SETTINGS — resume
                chimeSettings();
                gameFrameMs = millis();
                gameState = GAME_PLAYING;
            }
            if (buttons[0].pressed) {   // BACK — confirm quit
                chimeBack();
                gameState = GAME_READY;
            }
            return;
        }
        if (gameState == GAME_OVER) {
            if (buttons[1].pressed) {   // SETTINGS — retry
                chimeSettings();
                gameReset();
                gameState = GAME_PLAYING;
            }
            if (buttons[0].pressed) {   // BACK — exit to ready
                chimeBack();
                gameState = GAME_READY;
            }
            return;
        }
        return;
    }

    // ── Other screens ──
    // NEXT button (GPIO 2) — switch screen or sub-mode
    if (buttons[2].pressed) {
        chimeNext();
        if (inSettings) {
            if (settingsField == 0) {
                alarmHour = (alarmHour + 1) % 24;
            } else if (settingsField == 1) {
                alarmMinute = (alarmMinute + 1) % 60;
            } else if (settingsField == 2) {
                alarmEnabled = !alarmEnabled;
            } else {
                alarmSnoozeMin = (alarmSnoozeMin + 1) % 16; // 0-15 minutes
            }
        } else if (currentScreen == SCREEN_TIMER) {
            if (timerMode == TIMER_STOPWATCH) {
                timerMode = TIMER_CLOCK;
                currentScreen = (Screen)((currentScreen + 1) % SCREEN_COUNT);
            } else {
                timerMode = (TimerMode)((timerMode + 1) % TIMER_MODE_COUNT);
            }
        } else {
            currentScreen = (Screen)((currentScreen + 1) % SCREEN_COUNT);
        }
    }

    // SETTINGS button (GPIO 1)
    if (buttons[1].pressed) {
        chimeSettings();
        if (currentScreen == SCREEN_CLOCK && WiFi.status() != WL_CONNECTED) {
            // Enter manual time/date setting
            struct tm t;
            getLocalTime(&t, 100);
            clockSetHour  = t.tm_hour;
            clockSetMin   = t.tm_min;
            clockSetMonth = t.tm_mon + 1;
            clockSetDay   = t.tm_mday;
            clockSetYear  = t.tm_year + 1900;
            clockField = 0;
            clockInSettings = true;
        } else if (currentScreen == SCREEN_TIMER && timerMode == TIMER_ALARM) {
            if (inSettings) {
                settingsField = (settingsField + 1) % 4;
            } else {
                inSettings = true;
                settingsField = 0;
            }
        } else if (currentScreen == SCREEN_TIMER && timerMode == TIMER_STOPWATCH) {
            if (stopwatchRunning) {
                stopwatchElapsed += millis() - stopwatchStart;
                stopwatchRunning = false;
            } else {
                stopwatchStart = millis();
                stopwatchRunning = true;
            }
        }
    }

    // Long-press SETTINGS in stopwatch when stopped → reset
    if (buttons[1].holding && !buttons[1].holdFired &&
        (millis() - buttons[1].holdStart > HOLD_MS) &&
        currentScreen == SCREEN_TIMER && timerMode == TIMER_STOPWATCH && !stopwatchRunning) {
        buttons[1].holdFired = true;
        stopwatchElapsed = 0;
        chimeBack(); // feedback chime for reset
    }

    // BACK button (GPIO 0)
    if (buttons[0].pressed) {
        chimeBack();
        if (inSettings) {
            inSettings = false;
            alarmTriggered = false;
        } else if (currentScreen == SCREEN_TIMER && timerMode == TIMER_ALARM) {
            timerMode = TIMER_CLOCK;
        } else {
            currentScreen = (Screen)((currentScreen - 1 + SCREEN_COUNT) % SCREEN_COUNT);
        }
    }
}

// ── Alarm Check ─────────────────────────────────────────────────
void alarmStartRinging() {
    // Remember where user was before switching
    alarmPrevScreen = currentScreen;
    alarmRinging = true;
    alarmSnoozing = false;
    alarmLastChime = 0;  // force immediate first note
    alarmNoteIdx = 0;    // start melody from beginning
    currentScreen = SCREEN_TIMER;
    timerMode = TIMER_ALARM;
    inSettings = false;
}

void alarmStop() {
    alarmRinging = false;
    alarmSnoozing = false;
    ledcWriteTone(BUZZER_CH, 0); // silence buzzer immediately
}

void alarmSnooze() {
    alarmRinging = false;
    ledcWriteTone(BUZZER_CH, 0);
    alarmSnoozing = true;
    alarmSnoozeEnd = millis() + (unsigned long)alarmSnoozeMin * 60000UL;
    // Return to previous screen
    currentScreen = alarmPrevScreen;
}

void checkAlarm() {
    // Space Invaders-style continuous alarm melody (non-blocking, one note per step)
    if (alarmRinging) {
        // Melody: classic descending 4-note march + rising urgency phrase, looping
        static const uint16_t alarmMelody[] = {
            // Space Invaders march (bass)
            196, 0, 164, 0, 146, 0, 130, 0,   // G3 - E3 - D3 - C3
            196, 0, 164, 0, 146, 0, 130, 0,   // repeat
            // Rising urgency
            262, 0, 330, 0, 392, 0, 523, 0,   // C4 - E4 - G4 - C5
            // Attack phrase
            784, 0, 659, 0, 784, 0, 1047, 0,  // G5 - E5 - G5 - C6
            // Descend resolve
            880, 0, 784, 0, 659, 0, 523, 0,   // A5 - G5 - E5 - C5
        };
        static const int alarmMelodyLen = sizeof(alarmMelody) / sizeof(alarmMelody[0]);

        unsigned long now = millis();
        if (now - alarmLastChime >= 120) {  // 120ms per step
            alarmLastChime = now;
            uint16_t freq = alarmMelody[alarmNoteIdx];
            if (freq == 0) {
                ledcWriteTone(BUZZER_CH, 0); // rest
            } else {
                ledcWriteTone(BUZZER_CH, freq);
            }
            alarmNoteIdx = (alarmNoteIdx + 1) % alarmMelodyLen;
        }
        return;
    }

    // Check snooze expiry
    if (alarmSnoozing) {
        if (millis() >= alarmSnoozeEnd) {
            alarmStartRinging();
        }
        return;
    }

    if (!alarmEnabled) {
        alarmTriggered = false;
        return;
    }

    struct tm t;
    if (!getLocalTime(&t, 100)) return;

    if (t.tm_hour == alarmHour && t.tm_min == alarmMinute && !alarmTriggered) {
        alarmTriggered = true;
        alarmStartRinging();
    }
    if (t.tm_min != alarmMinute) {
        alarmTriggered = false;
    }
}

// ── Setup ───────────────────────────────────────────────────────
void setup() {
    // Silence buzzer as early as possible
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // LEDC buzzer setup
    ledcSetup(BUZZER_CH, 5000, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_CH);
    ledcWriteTone(BUZZER_CH, 0);

    Serial.begin(115200);
    delay(5000); // Wait for USB CDC host enumeration
    printf("\n\n[Boot] ===== Watch Starting =====\n");

    pinMode(BTN_BACK, INPUT_PULLUP);
    pinMode(BTN_SETTINGS, INPUT_PULLUP);
    pinMode(BTN_NEXT, INPUT_PULLUP);
    printf("[Boot] Pins configured\n");
    printf("[Boot] Buzzer ready\n");

    printf("[Boot] Initializing display...\n");
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(COL_BG);

    void* sprPtr = spr.createSprite(240, 240);
    printf("[Boot] Sprite: %s (free heap: %d bytes)\n",
           sprPtr ? "OK" : "FAILED", ESP.getFreeHeap());
    spr.setTextDatum(MC_DATUM);

    // Startup melody — gentle ascending arpeggio
    buzzerTone(392, 120);  // G4
    delay(30);
    buzzerTone(523, 120);  // C5
    delay(30);
    buzzerTone(659, 120);  // E5
    delay(30);
    buzzerTone(784, 200);  // G5 — held slightly longer to resolve

    connectWiFi();
    syncTime();
    updateWeather();
}

// ── Main Loop ───────────────────────────────────────────────────
void loop() {

    updateButtons();
    handleButtons();
    checkAlarm();
    updateWeather();
    updatePomodoro();
    updateGame();

    switch (currentScreen) {
        case SCREEN_CLOCK:    drawClockScreen();    break;
        case SCREEN_WEATHER:  drawWeatherScreen();  break;
        case SCREEN_TIMER:    drawTimerScreen();     break;
        case SCREEN_POMODORO: drawPomodoroScreen();  break;
        case SCREEN_GAME:     drawGameScreen();      break;
        default: break;
    }

    delay(50); // ~20fps
}
