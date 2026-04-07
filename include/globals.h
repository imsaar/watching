#ifndef GLOBALS_H
#define GLOBALS_H

#include <TFT_eSPI.h>

// ── Screen Enum ─────────────────────────────────────────────────
enum Screen { SCREEN_CLOCK = 0, SCREEN_WEATHER, SCREEN_TIMER, SCREEN_GAME, SCREEN_POMODORO, SCREEN_COUNT };

// ── Shared Display Objects ──────────────────────────────────────
extern TFT_eSPI tft;
extern TFT_eSprite spr;
extern Screen currentScreen;

// ── Shared Drawing Helpers ──────────────────────────────────────
void drawCircleBorder();
void drawScreenIndicator(int active);
int daysInMonth(int month, int year);

#endif
