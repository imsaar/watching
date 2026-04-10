/*
 * ESP32-C3 SuperMini Smart Watch
 * Round GC9A01 240x240 Display
 *
 * Wiring:
 *   GC9A01: SCK=GPIO5, MOSI=GPIO6, DC=GPIO7, CS=GPIO8, RST=GPIO9
 *   Buttons: BACK=GPIO0, SETTINGS=GPIO1, NEXT=GPIO2 (INPUT_PULLUP, active LOW)
 *   Buzzer: GPIO3 (passive piezo, PWM) — RTC GPIO, stays LOW during boot
 *
 * Screens:
 *   1) Watch face — time, Gregorian date, Islamic (Hijri) date
 *   2) Weather — current conditions + 3-day forecast (Lynnwood, WA via Open-Meteo)
 *   3) Timer — analog clock, alarm, stopwatch
 *   4) Geometry Dash — endless runner mini-game
 *   5) Pomodoro — configurable work/break timer
 */

#include <Arduino.h>
#include "config.h"
#include "globals.h"
#include "buttons.h"
#include "buzzer.h"
#include "wifi_ntp.h"
#include "weather.h"
#include "screen_clock.h"
#include "screen_weather.h"
#include "screen_timer.h"
#include "screen_game.h"
#include "screen_pomodoro.h"
#include "input_handler.h"
#include "storage.h"
#include "ota.h"
#include "screen_info.h"

void setup() {
    // Silence buzzer as early as possible
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // LEDC buzzer setup
    ledcSetup(BUZZER_CH, 5000, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_CH);
    ledcWriteTone(BUZZER_CH, 0);

    Serial.begin(115200);
    delay(5000); // Wait for USB CDC host enumeration
    printf("\n\n[Boot] ===== Watch Starting =====\n");

    pinMode(BTN_BACK, INPUT_PULLUP);
    pinMode(BTN_SETTINGS, INPUT_PULLUP);
    pinMode(BTN_NEXT, INPUT_PULLUP);
    printf("[Boot] Pins configured\n");
    printf("[Boot] Buzzer ready\n");

    printf("[Boot] Initializing display...\n");
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(COL_BG);

    void* sprPtr = spr.createSprite(240, 240);
    printf("[Boot] Sprite: %s (free heap: %d bytes)\n",
           sprPtr ? "OK" : "FAILED", ESP.getFreeHeap());
    spr.setTextDatum(MC_DATUM);

    // Startup melody — gentle ascending arpeggio
    buzzerTone(392, 120);  // G4
    delay(30);
    buzzerTone(523, 120);  // C5
    delay(30);
    buzzerTone(659, 120);  // E5
    delay(30);
    buzzerTone(784, 200);  // G5 — held slightly longer to resolve

    loadSettings();

    connectWiFi();
    syncTime();
    updateWeather();
    otaSetup();
}

void loop() {
    otaLoop();
    updateButtons();
    handleButtons();
    checkAlarm();
    updateWeather();
    updatePomodoro();
    updateGame();

    if (showInfoScreen) {
        drawInfoScreen();
    } else {
        switch (currentScreen) {
            case SCREEN_CLOCK:    drawClockScreen();    break;
            case SCREEN_WEATHER:  drawWeatherScreen();  break;
            case SCREEN_TIMER:    drawTimerScreen();    break;
            case SCREEN_GAME:     drawGameScreen();     break;
            case SCREEN_POMODORO: drawPomodoroScreen(); break;
            default: break;
        }
    }

    delay(50); // ~20fps
}
