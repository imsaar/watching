#include "buzzer.h"
#include "config.h"

void buzzerTone(uint32_t freq, uint32_t durationMs) {
    ledcWriteTone(BUZZER_CH, freq);
    delay(durationMs);
    ledcWriteTone(BUZZER_CH, 0);
}

void buzzerSilence() {
    ledcWriteTone(BUZZER_CH, 0);
}

void chimeBack() {
    buzzerTone(880, 60);
    delay(20);
    buzzerTone(660, 60);
}

void chimeSettings() {
    buzzerTone(1047, 50);
    delay(20);
    buzzerTone(1319, 50);
    delay(20);
    buzzerTone(1568, 50);
}

void chimeNext() {
    buzzerTone(1200, 80);
}

void chimeBreakStart() {
    buzzerTone(1047, 150);
    delay(30);
    buzzerTone(988, 150);
    delay(30);
    buzzerTone(784, 150);
    delay(30);
    buzzerTone(659, 200);
    delay(50);
    buzzerTone(523, 300);
}

void chimeBreakEnd() {
    buzzerTone(523, 100);
    delay(20);
    buzzerTone(659, 100);
    delay(20);
    buzzerTone(784, 100);
    delay(20);
    buzzerTone(1047, 150);
    delay(40);
    buzzerTone(1319, 150);
    delay(40);
    buzzerTone(1568, 250);
}

void chimeDeath() {
    buzzerTone(400, 80);
    delay(30);
    buzzerTone(250, 80);
    delay(30);
    buzzerTone(150, 150);
}

void chimeJump() {
    ledcWriteTone(BUZZER_CH, 900);
    delay(30);
    ledcWriteTone(BUZZER_CH, 0);
}
