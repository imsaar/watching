#ifndef SCREEN_CLOCK_H
#define SCREEN_CLOCK_H

extern bool clockInSettings;
extern int clockField;
extern int clockSetHour, clockSetMin, clockSetMonth, clockSetDay, clockSetYear;

void drawClockScreen();
void applyClockSettings();

#endif
