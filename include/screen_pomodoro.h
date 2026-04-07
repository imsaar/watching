#ifndef SCREEN_POMODORO_H
#define SCREEN_POMODORO_H

enum PomoState { POMO_IDLE, POMO_WORK, POMO_BREAK };

extern int pomoWorkMin;
extern int pomoBreakMin;
extern PomoState pomoState;
extern bool pomoRunning;
extern unsigned long pomoTimeLeft;
extern unsigned long pomoLastTick;
extern bool pomoInSettings;
extern int pomoField;
extern bool pomoEditing;

void drawPomodoroScreen();
void updatePomodoro();

#endif
