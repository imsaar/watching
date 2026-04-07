#include "screen_pomodoro.h"
#include "config.h"
#include "globals.h"
#include "buzzer.h"
#include <math.h>

int pomoWorkMin = 25;
int pomoBreakMin = 5;
PomoState pomoState = POMO_IDLE;
bool pomoRunning = false;
unsigned long pomoTimeLeft = 0;
unsigned long pomoLastTick = 0;
bool pomoInSettings = false;
int pomoField = 0;
bool pomoEditing = false;

void updatePomodoro() {
    if (!pomoRunning || pomoState == POMO_IDLE) return;

    unsigned long now = millis();
    unsigned long delta = now - pomoLastTick;
    pomoLastTick = now;

    if (delta >= pomoTimeLeft) {
        pomoTimeLeft = 0;
        pomoRunning = false;

        if (pomoState == POMO_WORK) {
            pomoState = POMO_BREAK;
            pomoTimeLeft = (unsigned long)pomoBreakMin * 60000UL;
            pomoLastTick = millis();
            pomoRunning = true;
            chimeBreakStart();
        } else {
            pomoState = POMO_IDLE;
            chimeBreakEnd();
        }
    } else {
        pomoTimeLeft -= delta;
    }
}

void drawPomodoroScreen() {
    spr.fillSprite(COL_BG);
    drawCircleBorder();
    drawScreenIndicator(SCREEN_POMODORO);

    spr.setTextDatum(MC_DATUM);

    if (pomoInSettings) {
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
