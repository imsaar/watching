#include "storage.h"
#include "screen_timer.h"
#include "screen_game.h"
#include "screen_pomodoro.h"
#include <Preferences.h>

static Preferences prefs;

void loadSettings() {
    prefs.begin("watch", true); // read-only

    // Alarm
    alarmEnabled   = prefs.getBool("alarmOn", false);
    alarmHour      = prefs.getInt("alarmHr", 7);
    alarmMinute    = prefs.getInt("alarmMin", 0);
    alarmSnoozeMin = prefs.getInt("snoozeMin", 5);

    // Game
    gameHiScore = prefs.getInt("hiScore", 0);

    // Pomodoro
    pomoWorkMin  = prefs.getInt("pomoWork", 25);
    pomoBreakMin = prefs.getInt("pomoBreak", 5);

    prefs.end();
    printf("[NVS] Settings loaded\n");
}

void saveAlarmSettings() {
    prefs.begin("watch", false);
    prefs.putBool("alarmOn", alarmEnabled);
    prefs.putInt("alarmHr", alarmHour);
    prefs.putInt("alarmMin", alarmMinute);
    prefs.putInt("snoozeMin", alarmSnoozeMin);
    prefs.end();
}

void saveGameHiScore() {
    prefs.begin("watch", false);
    prefs.putInt("hiScore", gameHiScore);
    prefs.end();
}

void savePomodoroSettings() {
    prefs.begin("watch", false);
    prefs.putInt("pomoWork", pomoWorkMin);
    prefs.putInt("pomoBreak", pomoBreakMin);
    prefs.end();
}
