# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-C3 SuperMini smart watch with a 240x240 round GC9A01 display. Features five screens (clock, weather, timer, geometry dash game, pomodoro), WiFi/NTP, weather API, OTA firmware updates, and piezo buzzer audio.

## Build & Upload

```bash
pio run                    # Build firmware
pio run -t upload          # Upload via USB serial
pio run -t uploadfs        # Upload filesystem (if needed)
```

No test suite exists. Verify changes by building (`pio run`) and testing on device.

## Hardware Configuration

- **MCU**: ESP32-C3 SuperMini (espressif32@6.9.0, Arduino framework)
- **Display**: GC9A01 240x240 round TFT via SPI (TFT_eSPI library)
- **Buttons**: 3 physical buttons (BACK=GPIO0, SETTINGS=GPIO1, NEXT=GPIO2) — active LOW, INPUT_PULLUP
- **Buzzer**: Piezo on GPIO3 via LEDC PWM
- **Partitions**: Custom dual-slot OTA (partitions.csv) with 1.9MB per app slot

## Architecture

### Main Loop (main.cpp)

Runs at ~20fps (50ms delay). Each iteration: OTA check → button debounce → input routing → update all modules → draw current screen. All rendering uses a single 240x240 TFT_eSprite (`spr`) that gets pushed to display at end of each screen's draw function.

### Screen System

Five screens defined as `enum Screen` in `globals.h`, cycled with NEXT/BACK buttons:

| Screen | Update | Draw | Header |
|--------|--------|------|--------|
| SCREEN_CLOCK | — | drawClockScreen() | screen_clock.h |
| SCREEN_WEATHER | updateWeather() | drawWeatherScreen() | screen_weather.h |
| SCREEN_TIMER | checkAlarm() | drawTimerScreen() | screen_timer.h |
| SCREEN_GAME | updateGame() | drawGameScreen() | screen_game.h |
| SCREEN_POMODORO | updatePomodoro() | drawPomodoroScreen() | screen_pomodoro.h |

All update functions run every loop regardless of which screen is active. Draw functions only run for the current screen.

### Input Routing (input_handler.cpp)

`handleButtons()` is a priority-based dispatcher: info screen overlay > alarm ringing > per-screen handlers. Each screen has its own sub-states (e.g., game has GAME_READY/PLAYING/OVER/CONFIRM_EXIT, timer has 3 sub-modes). Button routing logic lives entirely in `input_handler.cpp`, not in individual screen files.

### Rendering Pattern

Every screen follows the same pattern:
1. `spr.fillSprite(COL_BG)` — clear buffer
2. `drawCircleBorder()` — round display mask
3. Draw screen content
4. `spr.pushSprite(0, 0)` — flush to display

Colors are RGB565 constants defined in `config.h` (COL_WHITE, COL_CYAN, COL_RED, etc.).

### Persistence (storage.cpp)

Uses ESP32 NVS (Preferences library). Persisted values: alarm settings, pomodoro durations, game high score. Access via `saveAlarmSettings()`, `loadAlarmSettings()`, `saveGameHiScore()`, etc.

### OTA System (ota.cpp)

Dual-slot OTA with rollback protection. Custom web page at `/update` for firmware upload. Boot guard: FreeRTOS task auto-reboots if setup() exceeds 30s. Hold BACK+NEXT during boot for manual rollback. 3-button emergency reboot (hold all 2s).

### Game Engine (screen_game.cpp)

Geometry Dash clone with gravity physics, 9 obstacle types (spikes, platforms, ramps, saw blades, slopes, staircases), particle system, parallax scrolling, progressive difficulty (speed increases every 5 points, theme changes every 10). Obstacle spawning uses probability tables that shift based on score thresholds.

## Key Constraints

- **240x240 circular display**: Content outside the circle is masked. Usable area is roughly a 232px diameter circle.
- **ESP32-C3 memory**: Limited RAM (~320KB). Single sprite buffer, max 8 obstacles, 20 particles, 18 stars.
- **No floating-point unit**: ESP32-C3 lacks hardware FPU. Float math works but is software-emulated.
- **3-button input only**: All UI must work with just BACK, SETTINGS, NEXT.
- **Firmware version**: Defined as `FW_VERSION` in `config.h`. Update when making releases.
