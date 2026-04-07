#include "screen_clock.h"
#include "config.h"
#include "globals.h"
#include "hijri.h"
#include "weather.h"
#include <WiFi.h>
#include <time.h>
#include <math.h>

bool clockInSettings = false;
int clockField = 0;
int clockSetHour, clockSetMin, clockSetMonth, clockSetDay, clockSetYear;

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

void drawClockScreen() {
    struct tm t;
    getLocalTime(&t, 100);

    spr.fillSprite(COL_BG);
    drawCircleBorder();
    drawScreenIndicator(0);
    spr.setTextDatum(MC_DATUM);

    if (clockInSettings) {
        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString("Set Time & Date", 120, 25, 2);

        const char* labels[] = {"Hour", "Minute", "Month", "Day", "Year"};

        char hBuf[3], mBuf[3];
        sprintf(hBuf, "%02d", clockSetHour);
        sprintf(mBuf, "%02d", clockSetMin);

        spr.setTextColor(clockField == 0 ? COL_GREEN : COL_WHITE, COL_BG);
        spr.drawString(hBuf, 90, 70, 7);
        spr.setTextColor(COL_MID_GRAY, COL_BG);
        spr.drawString(":", 120, 65, 6);
        spr.setTextColor(clockField == 1 ? COL_GREEN : COL_WHITE, COL_BG);
        spr.drawString(mBuf, 150, 70, 7);

        const char* monthNames[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};
        char dateBuf[20];
        sprintf(dateBuf, "%s %d, %d", monthNames[clockSetMonth - 1], clockSetDay, clockSetYear);

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

        spr.setTextColor(COL_YELLOW, COL_BG);
        char fieldLabel[20];
        sprintf(fieldLabel, "< > %s", labels[clockField]);
        spr.drawString(fieldLabel, 120, 160, 2);

        spr.setTextColor(COL_MID_GRAY, COL_BG);
        spr.drawString("SET=Field NEXT=+ BACK=Done", 120, 195, 1);

        spr.pushSprite(0, 0);
        return;
    }

    // Normal clock face
    char timeBuf[6];
    int hour12 = t.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    sprintf(timeBuf, "%d:%02d", hour12, t.tm_min);
    spr.setTextColor(COL_WHITE, COL_BG);
    spr.drawString(timeBuf, 115, 65, 7);

    int ix = 42, iy = 65;
    if (t.tm_hour < 12) {
        spr.fillCircle(ix, iy, 5, COL_YELLOW);
        for (int i = 0; i < 8; i++) {
            float a = i * 45.0 * DEG_TO_RAD;
            spr.drawLine(ix + (int)(7 * cos(a)), iy + (int)(7 * sin(a)),
                         ix + (int)(10 * cos(a)), iy + (int)(10 * sin(a)), COL_YELLOW);
        }
    } else {
        spr.fillCircle(ix, iy, 6, COL_CYAN);
        spr.fillCircle(ix + 4, iy - 3, 5, COL_BG);
    }

    for (int s = 0; s <= t.tm_sec; s++) {
        float a = s * 6.0 - 90.0;
        float rad = a * DEG_TO_RAD;
        int x = 120 + (int)(105 * cos(rad));
        int y = 120 + (int)(105 * sin(rad));
        spr.fillCircle(x, y, 2, COL_TEAL);
    }

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

    HijriDate h = gregorianToHijri(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    char hijriBuf[40];
    sprintf(hijriBuf, "%d %s %d", h.day, hijriMonths[h.month - 1], h.year);
    spr.setTextColor(COL_GOLD, COL_BG);
    spr.drawString(hijriBuf, 120, 160, 2);

    spr.drawFastHLine(50, 178, 140, COL_DARK_GRAY);

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
