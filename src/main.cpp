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

// ── Background watchdog task ──
// Runs as a FreeRTOS task independent of setup()/loop().
//  1) Emergency reboot: hold all 3 buttons for 2 seconds (works in any state)
//  2) OTA boot guard: if setup() doesn't finish within 30 seconds after an
//     OTA update, force a reset so the bootloader auto-rolls back to the
//     previous firmware (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is active).
static volatile bool bootValidated = false;

static void rebootWatchTask(void*) {
    unsigned long allHeldSince = 0;
    unsigned long bootDeadline = millis() + 30000;
    for (;;) {
        // ── 3-button emergency reboot ──
        bool allPressed = !digitalRead(BTN_BACK) &&
                          !digitalRead(BTN_SETTINGS) &&
                          !digitalRead(BTN_NEXT);
        if (allPressed) {
            if (allHeldSince == 0) allHeldSince = millis();
            if (millis() - allHeldSince >= 2000) {
                // Long descending chime to confirm reboot
                ledcWriteTone(BUZZER_CH, 1000);
                delay(200);
                ledcWriteTone(BUZZER_CH, 600);
                delay(200);
                ledcWriteTone(BUZZER_CH, 300);
                delay(400);
                ledcWriteTone(BUZZER_CH, 0);
                ESP.restart();
            }
        } else {
            allHeldSince = 0;
        }

        // ── OTA boot guard ──
        if (!bootValidated && millis() > bootDeadline) {
            printf("[OTA] Boot timeout — setup() did not complete.\n");
            printf("[OTA] Forcing reset for bootloader rollback...\n");
            ESP.restart();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void setup() {
    // Silence buzzer as early as possible
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // LEDC buzzer setup
    ledcSetup(BUZZER_CH, 5000, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_CH);
    ledcWriteTone(BUZZER_CH, 0);

    pinMode(BTN_BACK, INPUT_PULLUP);
    pinMode(BTN_SETTINGS, INPUT_PULLUP);
    pinMode(BTN_NEXT, INPUT_PULLUP);

    // Start watchdog task FIRST — before display, WiFi, or anything that could hang.
    // Provides: emergency 3-button reboot + 30s OTA boot guard.
    xTaskCreate(rebootWatchTask, "reboot", 2048, NULL, 1, NULL);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(COL_BG);

    void* sprPtr = spr.createSprite(240, 240);
    spr.setTextDatum(MC_DATUM);

    // ── Rollback check: hold BACK + NEXT during boot ──
    delay(100);  // let pins settle
    if (!digitalRead(BTN_BACK) && !digitalRead(BTN_NEXT)) {
        spr.fillSprite(COL_BG);
        spr.setTextColor(COL_ORANGE, COL_BG);
        spr.drawString("Rollback?", 120, 80, 4);
        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString("Release to cancel", 120, 130, 2);
        spr.drawString("Keep holding 3s...", 120, 155, 2);
        spr.pushSprite(0, 0);

        unsigned long holdStart = millis();
        bool held = true;
        while (millis() - holdStart < 3000) {
            if (digitalRead(BTN_BACK) || digitalRead(BTN_NEXT)) {
                held = false;
                break;
            }
            delay(50);
        }

        if (held) {
            if (otaRollback()) {
                spr.fillSprite(COL_BG);
                spr.setTextColor(COL_GREEN, COL_BG);
                spr.drawString("Rolling back!", 120, 100, 4);
                spr.setTextColor(COL_WHITE, COL_BG);
                spr.drawString("Rebooting...", 120, 140, 2);
                spr.pushSprite(0, 0);
                delay(1000);
                ESP.restart();
            } else {
                spr.fillSprite(COL_BG);
                spr.setTextColor(COL_RED, COL_BG);
                spr.drawString("No previous", 120, 90, 4);
                spr.drawString("version found", 120, 120, 4);
                spr.pushSprite(0, 0);
                delay(2000);
            }
        }
    }

    // Show boot screen while waiting for serial CDC
    spr.fillSprite(COL_BG);
    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString("Booting...", 120, 120, 4);
    spr.pushSprite(0, 0);

    Serial.begin(115200);
    delay(2000); // Wait for USB CDC host enumeration
    printf("\n\n[Boot] ===== Watch v" FW_VERSION " Starting =====\n");
    printf("[Boot] Flash: %d KB (mode: %d, speed: %d MHz)\n",
           ESP.getFlashChipSize() / 1024,
           ESP.getFlashChipMode(),
           ESP.getFlashChipSpeed() / 1000000);
    printf("[Boot] Sprite: %s (free heap: %d bytes)\n",
           sprPtr ? "OK" : "FAILED", ESP.getFreeHeap());

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

    // Mark this firmware as valid — disarms the boot guard timer
    // and tells the bootloader this OTA image is good (no rollback needed).
    otaValidateApp();
    bootValidated = true;
    printf("[Boot] Setup complete, boot validated.\n");
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
