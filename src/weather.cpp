#include "weather.h"
#include "config.h"
#include "globals.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

WeatherData currentWeather;
ForecastDay forecast[3];
bool weatherValid = false;
static unsigned long lastWeatherFetch = 0;

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

void drawCloud(int cx, int cy, int r) {
    spr.fillCircle(cx, cy - r/2, r, COL_WHITE);
    spr.fillCircle(cx - r, cy, r * 3/4, COL_WHITE);
    spr.fillCircle(cx + r, cy, r * 3/4, COL_WHITE);
    spr.fillRect(cx - r, cy - r/4, r * 2, r, COL_WHITE);
}

void drawWeatherIcon(int cx, int cy, int code, int s) {
    if (code == 0) {
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
        drawCloud(cx, cy, 3 * s);
    } else if (code == 45 || code == 48) {
        for (int i = 0; i < 3; i++) {
            int ly = cy - 3 * s + i * 3 * s;
            spr.drawFastHLine(cx - 5 * s, ly, 10 * s, COL_MID_GRAY);
        }
    } else if ((code >= 51 && code <= 57) || (code >= 61 && code <= 67) || (code >= 80 && code <= 82)) {
        int r = 2 * s;
        drawCloud(cx, cy - r, r);
        uint16_t dropCol = COL_BLUE;
        for (int d = -1; d <= 1; d++) {
            int dx = cx + d * 3 * s;
            spr.drawLine(dx, cy + s, dx - s, cy + s + 2 * s, dropCol);
        }
    } else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
        int r = 2 * s;
        drawCloud(cx, cy - r, r);
        spr.fillCircle(cx - 2 * s, cy + s + s, s, COL_CYAN);
        spr.fillCircle(cx, cy + 2 * s + s, s, COL_CYAN);
        spr.fillCircle(cx + 2 * s, cy + s + s, s, COL_CYAN);
    } else if (code >= 95) {
        int r = 2 * s;
        drawCloud(cx, cy - r, r);
        spr.drawLine(cx, cy + s, cx - s, cy + 2 * s, COL_YELLOW);
        spr.drawLine(cx - s, cy + 2 * s, cx + s / 2, cy + 2 * s, COL_YELLOW);
        spr.drawLine(cx + s / 2, cy + 2 * s, cx - s / 2, cy + 3 * s, COL_YELLOW);
    }
}

static void fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
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

        JsonObject current = doc["current"];
        currentWeather.temp        = current["temperature_2m"];
        currentWeather.feelsLike   = current["apparent_temperature"];
        currentWeather.humidity    = current["relative_humidity_2m"];
        currentWeather.weatherCode = current["weather_code"];
        currentWeather.windSpeed   = current["wind_speed_10m"];

        JsonArray dailyCodes   = doc["daily"]["weather_code"];
        JsonArray dailyMaxTemp = doc["daily"]["temperature_2m_max"];
        JsonArray dailyMinTemp = doc["daily"]["temperature_2m_min"];
        JsonArray dailyTime    = doc["daily"]["time"];

        for (int i = 0; i < 3 && i + 1 < (int)dailyCodes.size(); i++) {
            forecast[i].weatherCode = dailyCodes[i + 1];
            forecast[i].tempMax     = dailyMaxTemp[i + 1];
            forecast[i].tempMin     = dailyMinTemp[i + 1];

            String dateStr = dailyTime[i + 1].as<String>();
            int yr = dateStr.substring(0, 4).toInt();
            int mo = dateStr.substring(5, 7).toInt();
            int dy = dateStr.substring(8, 10).toInt();
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
