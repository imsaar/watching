#ifndef SCREEN_TIMER_H
#define SCREEN_TIMER_H

#include "globals.h"

enum TimerMode { TIMER_CLOCK = 0, TIMER_ALARM, TIMER_STOPWATCH, TIMER_MODE_COUNT };
extern TimerMode timerMode;

// Alarm state
extern bool alarmEnabled;
extern int alarmHour;
extern int alarmMinute;
extern bool alarmTriggered;
extern bool alarmRinging;
extern unsigned long alarmLastChime;
extern int alarmNoteIdx;
extern int alarmSnoozeMin;
extern bool alarmSnoozing;
extern unsigned long alarmSnoozeEnd;
extern Screen alarmPrevScreen;

// Alarm settings
extern bool inSettings;
extern int settingsField;

// Stopwatch state
extern bool stopwatchRunning;
extern unsigned long stopwatchStart;
extern unsigned long stopwatchElapsed;

void drawTimerScreen();
void checkAlarm();
void alarmStartRinging();
void alarmStop();
void alarmSnooze();

#endif
