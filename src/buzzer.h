#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>

void buzzerTone(uint32_t freq, uint32_t durationMs);
void buzzerSilence();
void chimeBack();
void chimeSettings();
void chimeNext();
void chimeBreakStart();
void chimeBreakEnd();
void chimeDeath();
void chimeJump();

#endif
