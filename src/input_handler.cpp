#include "input_handler.h"
#include "config.h"
#include "globals.h"
#include "buttons.h"
#include "buzzer.h"
#include "screen_clock.h"
#include "screen_timer.h"
#include "screen_pomodoro.h"
#include "screen_game.h"
#include <WiFi.h>
#include <time.h>

void handleButtons() {
    // ── Alarm ringing — global intercept ──
    if (alarmRinging) {
        if (buttons[0].pressed) {
            chimeBack();
            alarmStop();
        }
        if (buttons[2].pressed) {
            if (alarmSnoozeMin > 0) {
                chimeNext();
                alarmSnooze();
            }
        }
        return;
    }

    // ── Clock settings (manual time/date when no WiFi) ──
    if (clockInSettings) {
        if (buttons[1].pressed) {
            chimeSettings();
            clockField = (clockField + 1) % 5;
        }
        if (buttons[2].pressed) {
            chimeNext();
            switch (clockField) {
                case 0: clockSetHour = (clockSetHour + 1) % 24; break;
                case 1: clockSetMin = (clockSetMin + 1) % 60; break;
                case 2: clockSetMonth = (clockSetMonth % 12) + 1; break;
                case 3:
                    clockSetDay = (clockSetDay % daysInMonth(clockSetMonth, clockSetYear)) + 1;
                    break;
                case 4: clockSetYear++; break;
            }
        }
        if (buttons[0].pressed) {
            chimeBack();
            applyClockSettings();
            clockInSettings = false;
        }
        return;
    }

    // ── Pomodoro screen buttons ──
    if (currentScreen == SCREEN_POMODORO) {
        if (pomoInSettings) {
            if (buttons[2].pressed) {
                chimeNext();
                if (pomoEditing) {
                    if (pomoField == 0) pomoWorkMin = min(pomoWorkMin + 1, 99);
                    else                pomoBreakMin = min(pomoBreakMin + 1, 99);
                } else {
                    pomoField = (pomoField + 1) % 2;
                }
            }
            if (buttons[0].pressed) {
                chimeBack();
                if (pomoEditing) {
                    if (pomoField == 0) pomoWorkMin = max(pomoWorkMin - 1, 1);
                    else                pomoBreakMin = max(pomoBreakMin - 1, 1);
                } else {
                    pomoInSettings = false;
                }
            }
            if (buttons[1].pressed) {
                chimeSettings();
                if (pomoEditing) {
                    pomoEditing = false;
                } else {
                    pomoEditing = true;
                }
            }
            return;
        }

        if (buttons[2].pressed) {
            chimeNext();
            if (pomoState == POMO_IDLE) {
                pomoState = POMO_WORK;
                pomoTimeLeft = (unsigned long)pomoWorkMin * 60000UL;
                pomoLastTick = millis();
                pomoRunning = true;
            } else {
                if (pomoRunning) {
                    pomoRunning = false;
                } else {
                    pomoLastTick = millis();
                    pomoRunning = true;
                }
            }
        }
        if (buttons[0].pressed) {
            chimeBack();
            pomoState = POMO_IDLE;
            pomoRunning = false;
            pomoTimeLeft = 0;
        }
        if (buttons[1].pressed) {
            if (pomoState == POMO_IDLE) {
                chimeSettings();
                pomoInSettings = true;
                pomoField = 0;
                pomoEditing = false;
            }
        }
        // Long-press NEXT — navigate to next screen
        if (buttons[2].holding && !buttons[2].holdFired &&
            (millis() - buttons[2].holdStart > HOLD_MS)) {
            buttons[2].holdFired = true;
            chimeNext();
            currentScreen = (Screen)((currentScreen + 1) % SCREEN_COUNT);
        }
        // Long-press BACK — exit to watch face
        if (buttons[0].holding && !buttons[0].holdFired &&
            (millis() - buttons[0].holdStart > HOLD_MS)) {
            buttons[0].holdFired = true;
            pomoState = POMO_IDLE;
            pomoRunning = false;
            pomoTimeLeft = 0;
            currentScreen = SCREEN_CLOCK;
            chimeBack();
        }
        return;
    }

    // ── Game screen buttons ──
    if (currentScreen == SCREEN_GAME) {
        if (gameState == GAME_READY) {
            if (buttons[1].pressed) {
                chimeSettings();
                gameReset();
                gameState = GAME_PLAYING;
            }
            if (buttons[2].pressed) {
                chimeNext();
                currentScreen = (Screen)((currentScreen + 1) % SCREEN_COUNT);
            }
            if (buttons[0].pressed) {
                chimeBack();
                currentScreen = (Screen)((currentScreen - 1 + SCREEN_COUNT) % SCREEN_COUNT);
            }
            return;
        }
        if (gameState == GAME_PLAYING) {
            if (buttons[2].pressed) {
                if (gameOnGround) {
                    chimeJump();
                    gameVelY = GAME_JUMP_VEL;
                    gameOnGround = false;
                    gameCanDouble = true;
                    for (int p = 0; p < 4; p++) {
                        gameSpawnParticle(
                            GAME_PLAYER_X - 6 + random(12),
                            GAME_GROUND_Y,
                            -1.0f + random(200) / 100.0f, -1.0f - random(100) / 100.0f,
                            gameGroundCol + 0x2104, 5 + random(3));
                    }
                } else if (gameCanDouble) {
                    chimeJump();
                    gameVelY = GAME_JUMP_VEL * 0.85f;
                    gameCanDouble = false;
                    gamePlayerRot += 0.5f;
                    for (int p = 0; p < 3; p++) {
                        gameSpawnParticle(
                            GAME_PLAYER_X, gamePlayerY + GAME_PLAYER_SZ,
                            -1.5f + random(300) / 100.0f, 1.0f + random(100) / 100.0f,
                            gameAccentCol, 6);
                    }
                }
            }
            if (buttons[0].pressed) {
                gameState = GAME_CONFIRM_EXIT;
            }
            return;
        }
        if (gameState == GAME_CONFIRM_EXIT) {
            if (buttons[1].pressed) {
                chimeSettings();
                gameFrameMs = millis();
                gameState = GAME_PLAYING;
            }
            if (buttons[0].pressed) {
                chimeBack();
                gameState = GAME_READY;
            }
            return;
        }
        if (gameState == GAME_OVER) {
            if (buttons[1].pressed) {
                chimeSettings();
                gameReset();
                gameState = GAME_PLAYING;
            }
            if (buttons[0].pressed) {
                chimeBack();
                gameState = GAME_READY;
            }
            return;
        }
        return;
    }

    // ── Other screens ──
    if (buttons[2].pressed) {
        chimeNext();
        if (inSettings) {
            if (settingsField == 0) {
                alarmHour = (alarmHour + 1) % 24;
            } else if (settingsField == 1) {
                alarmMinute = (alarmMinute + 1) % 60;
            } else if (settingsField == 2) {
                alarmEnabled = !alarmEnabled;
            } else {
                alarmSnoozeMin = (alarmSnoozeMin + 1) % 16;
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

    if (buttons[1].pressed) {
        chimeSettings();
        if (currentScreen == SCREEN_CLOCK && WiFi.status() != WL_CONNECTED) {
            struct tm t;
            getLocalTime(&t, 100);
            clockSetHour  = t.tm_hour;
            clockSetMin   = t.tm_min;
            clockSetMonth = t.tm_mon + 1;
            clockSetDay   = t.tm_mday;
            clockSetYear  = t.tm_year + 1900;
            clockField = 0;
            clockInSettings = true;
        } else if (currentScreen == SCREEN_TIMER && timerMode == TIMER_ALARM) {
            if (inSettings) {
                settingsField = (settingsField + 1) % 4;
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
        chimeBack();
    }

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
