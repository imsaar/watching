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
enum Screen { SCREEN_CLOCK = 0, SCREEN_WEATHER, SCREEN_TIMER, SCREEN_COUNT };
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
    for (int i = 0; i < 5; i++) {
        buzzerTone(2000, 150);
        delay(50);
        buzzerTone(1500, 150);
        delay(50);
    }
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
bool alarmTriggered = false;

// ── Timer Screen Sub-modes ──────────────────────────────────────
enum TimerMode { TIMER_CLOCK = 0, TIMER_ALARM, TIMER_STOPWATCH, TIMER_MODE_COUNT };
TimerMode timerMode = TIMER_CLOCK;

// ── Settings State ──────────────────────────────────────────────
bool inSettings = false;
int settingsField = 0; // 0=hour, 1=minute, 2=enable/disable

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
    for (int i = 0; i < 3; i++) {
        int x = 108 + i * 12;
        int y = 228;
        if (i == active) {
            spr.fillCircle(x, y, 4, COL_CYAN);
        } else {
            spr.drawCircle(x, y, 3, COL_DARK_GRAY);
        }
    }
}

// ── Screen 1: Clock Face ────────────────────────────────────────
void drawClockScreen() {
    struct tm t;
    getLocalTime(&t, 100);

    spr.fillSprite(COL_BG);
    drawCircleBorder();
    drawScreenIndicator(0);

    // Time — large centered
    char timeBuf[6];
    int hour12 = t.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    sprintf(timeBuf, "%d:%02d", hour12, t.tm_min);
    const char* ampm = (t.tm_hour < 12) ? "AM" : "PM";

    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(COL_WHITE, COL_BG);
    spr.drawString(timeBuf, 115, 65, 7);

    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString(ampm, 195, 50, 4);

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

    // Current temp — big
    char tempBuf[10];
    sprintf(tempBuf, "%.0f°F", currentWeather.temp);
    spr.setTextColor(COL_WHITE, COL_BG);
    spr.drawString(tempBuf, 120, 52, 6);

    // Description with icon
    drawWeatherIcon(60, 82, currentWeather.weatherCode, 2);
    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(wmoColor(currentWeather.weatherCode), COL_BG);
    spr.drawString(wmoDescription(currentWeather.weatherCode), 85, 82, 2);
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
        spr.drawString(alarmEnabled ? "ON" : "OFF", 120, 145, 4);

        if (inSettings) {
            spr.setTextColor(COL_YELLOW, COL_BG);
            const char* fields[] = {"< > Hour", "< > Minute", "< > On/Off"};
            spr.drawString(fields[settingsField], 120, 175, 2);
            spr.setTextColor(COL_MID_GRAY, COL_BG);
            spr.drawString("NEXT=Adjust BACK=Done", 120, 195, 1);
        } else {
            spr.setTextColor(COL_MID_GRAY, COL_BG);
            spr.drawString("SETTINGS to edit", 120, 180, 2);
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

// ── Handle Button Actions ───────────────────────────────────────
void handleButtons() {
    // NEXT button (GPIO 2) — switch screen or sub-mode
    if (buttons[2].pressed) {
        chimeNext();
        if (inSettings) {
            if (settingsField == 0) {
                alarmHour = (alarmHour + 1) % 24;
            } else if (settingsField == 1) {
                alarmMinute = (alarmMinute + 1) % 60;
            } else {
                alarmEnabled = !alarmEnabled;
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
        if (currentScreen == SCREEN_TIMER && timerMode == TIMER_ALARM) {
            if (inSettings) {
                settingsField = (settingsField + 1) % 3;
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
void checkAlarm() {
    if (!alarmEnabled) {
        alarmTriggered = false;
        return;
    }

    struct tm t;
    if (!getLocalTime(&t, 100)) return;

    if (t.tm_hour == alarmHour && t.tm_min == alarmMinute && !alarmTriggered) {
        alarmTriggered = true;
        chimeAlarm();
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

    switch (currentScreen) {
        case SCREEN_CLOCK:   drawClockScreen();   break;
        case SCREEN_WEATHER: drawWeatherScreen();  break;
        case SCREEN_TIMER:   drawTimerScreen();    break;
        default: break;
    }

    delay(50); // ~20fps
}
