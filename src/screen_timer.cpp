#include "screen_timer.h"
#include "config.h"
#include "globals.h"
#include "hijri.h"
#include "buzzer.h"
#include <time.h>
#include <math.h>

TimerMode timerMode = TIMER_CLOCK;

// Alarm state
bool alarmEnabled = false;
int alarmHour = 7;
int alarmMinute = 0;
bool alarmTriggered = false;
bool alarmRinging = false;
unsigned long alarmLastChime = 0;
int alarmNoteIdx = 0;
int alarmSnoozeMin = 5;
bool alarmSnoozing = false;
unsigned long alarmSnoozeEnd = 0;
Screen alarmPrevScreen = SCREEN_CLOCK;

// Alarm settings
bool inSettings = false;
int settingsField = 0;

// Stopwatch state
bool stopwatchRunning = false;
unsigned long stopwatchStart = 0;
unsigned long stopwatchElapsed = 0;

void alarmStartRinging() {
    alarmPrevScreen = currentScreen;
    alarmRinging = true;
    alarmSnoozing = false;
    alarmLastChime = 0;
    alarmNoteIdx = 0;
    currentScreen = SCREEN_TIMER;
    timerMode = TIMER_ALARM;
    inSettings = false;
}

void alarmStop() {
    alarmRinging = false;
    alarmSnoozing = false;
    buzzerSilence();
}

void alarmSnooze() {
    alarmRinging = false;
    buzzerSilence();
    alarmSnoozing = true;
    alarmSnoozeEnd = millis() + (unsigned long)alarmSnoozeMin * 60000UL;
    currentScreen = alarmPrevScreen;
}

void checkAlarm() {
    // Space Invaders-style continuous alarm melody
    if (alarmRinging) {
        static const uint16_t alarmMelody[] = {
            196, 0, 164, 0, 146, 0, 130, 0,
            196, 0, 164, 0, 146, 0, 130, 0,
            262, 0, 330, 0, 392, 0, 523, 0,
            784, 0, 659, 0, 784, 0, 1047, 0,
            880, 0, 784, 0, 659, 0, 523, 0,
        };
        static const int alarmMelodyLen = sizeof(alarmMelody) / sizeof(alarmMelody[0]);

        unsigned long now = millis();
        if (now - alarmLastChime >= 120) {
            alarmLastChime = now;
            uint16_t freq = alarmMelody[alarmNoteIdx];
            if (freq == 0) {
                buzzerSilence();
            } else {
                ledcWriteTone(BUZZER_CH, freq);
            }
            alarmNoteIdx = (alarmNoteIdx + 1) % alarmMelodyLen;
        }
        return;
    }

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

void drawTimerScreen() {
    struct tm t;
    getLocalTime(&t, 100);

    spr.fillSprite(COL_BG);
    drawCircleBorder();
    drawScreenIndicator(2);

    spr.setTextDatum(MC_DATUM);

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

        float hAngle = ((t.tm_hour % 12) + t.tm_min / 60.0) * 30.0 - 90.0;
        float hRad = hAngle * DEG_TO_RAD;
        int hLen = 45;
        int hx = cx + (int)(hLen * cos(hRad));
        int hy = cy + (int)(hLen * sin(hRad));
        spr.drawLine(cx, cy, hx, hy, COL_WHITE);
        spr.drawLine(cx + 1, cy, hx + 1, hy, COL_WHITE);
        spr.drawLine(cx, cy + 1, hx, hy + 1, COL_WHITE);

        float mAngle = t.tm_min * 6.0 - 90.0;
        float mRad = mAngle * DEG_TO_RAD;
        int mLen = 62;
        int mx = cx + (int)(mLen * cos(mRad));
        int my = cy + (int)(mLen * sin(mRad));
        spr.drawLine(cx, cy, mx, my, COL_CYAN);
        spr.drawLine(cx + 1, cy, mx + 1, my, COL_CYAN);

        float sAngle = t.tm_sec * 6.0 - 90.0;
        float sRad = sAngle * DEG_TO_RAD;
        int sLen = 68;
        int sx = cx + (int)(sLen * cos(sRad));
        int sy = cy + (int)(sLen * sin(sRad));
        spr.drawLine(cx, cy, sx, sy, COL_RED);

        spr.fillCircle(cx, cy, 3, COL_WHITE);

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
        if (alarmRinging) {
            bool flash = (millis() / 400) % 2;
            spr.setTextColor(flash ? COL_RED : COL_YELLOW, COL_BG);
            spr.drawString("ALARM!", 120, 50, 4);

            char alarmBuf[6];
            int aH = alarmHour % 12;
            if (aH == 0) aH = 12;
            sprintf(alarmBuf, "%d:%02d", aH, alarmMinute);
            spr.setTextColor(COL_WHITE, COL_BG);
            spr.drawString(alarmBuf, 110, 95, 7);
            const char* aAmPm = (alarmHour < 12) ? "AM" : "PM";
            spr.drawString(aAmPm, 190, 80, 4);

            int bellX = 120, bellY = 140;
            int swing = ((millis() / 200) % 3) - 1;
            spr.fillCircle(bellX + swing, bellY - 8, 3, COL_YELLOW);
            spr.fillTriangle(bellX - 10 + swing, bellY, bellX + 10 + swing, bellY,
                             bellX + swing, bellY - 14, COL_YELLOW);
            spr.drawFastHLine(bellX - 12 + swing, bellY + 1, 24, COL_YELLOW);
            spr.fillCircle(bellX + swing, bellY + 4, 2, COL_YELLOW);

            if (alarmSnoozeMin > 0) {
                spr.setTextColor(COL_GREEN, COL_BG);
                char snoozeBuf[24];
                sprintf(snoozeBuf, "NEXT = Snooze %dm", alarmSnoozeMin);
                spr.drawString(snoozeBuf, 120, 170, 2);
            }
            spr.setTextColor(COL_RED, COL_BG);
            spr.drawString("BACK = Stop", 120, 190, 2);

        } else if (alarmSnoozing) {
            spr.setTextColor(COL_CYAN, COL_BG);
            spr.drawString("Snoozing", 120, 50, 4);

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

            char alarmBuf[12];
            int aH = alarmHour % 12;
            if (aH == 0) aH = 12;
            sprintf(alarmBuf, "%d:%02d %s", aH, alarmMinute, (alarmHour < 12) ? "AM" : "PM");
            spr.setTextColor(COL_GOLD, COL_BG);
            spr.drawString(alarmBuf, 120, 175, 2);

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
