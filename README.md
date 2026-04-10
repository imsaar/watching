# ESP32-C3 SuperMini Smart Watch

A functional and stylish smart watch built with an ESP32-C3 SuperMini and a round GC9A01 display. This project features a multi-screen interface, real-time weather updates, Hijri calendar support, a complete timer/alarm system, a Pomodoro timer, and a Geometry Dash mini-game.

## 🚀 Features

-   **Watch Face**:
    -   12-hour time with a smooth seconds arc and sun/moon AM/PM icon.
    -   Gregorian and Islamic (Hijri) date display.
    -   WiFi connectivity and current weather summary.
    -   Manual time/date setting via SETTINGS button when WiFi is unavailable.
-   **Weather Dashboard**:
    -   Current temperature, "feels like", humidity, and wind speed with weather condition icon.
    -   3-day forecast with weather icons and daily highs/lows.
    -   Powered by the Open-Meteo API (no API key required).
-   **Timer & Utilities**:
    -   **Analog Clock**: NTP-synced analog clock face with hour/minute/second hands, hour numbers, sun/moon icon, and compact Gregorian and Hijri date display.
    -   **Alarm**: Configurable alarm with continuous Space Invaders-style melody. Configurable snooze duration (0-15 minutes, 0 disables snooze). Alarm auto-switches to the alarm screen when triggered, with Snooze (NEXT) and Stop (BACK) options. Snooze countdown re-triggers the alarm when it expires.
    -   **Stopwatch**: High-precision stopwatch with millisecond display. Start/stop with SETTINGS button, long-press SETTINGS to reset when stopped.
-   **Geometry Dash Game**:
    -   Endless runner with a jumping square dodging obstacles and navigating terrain.
    -   Seven obstacle types introduced progressively: single spikes, double spikes, tall spikes, landable platforms, platforms with spikes, launch ramps, and spinning saw blades.
    -   Platforms you can land and run on; ramps that auto-launch you upward with a speed boost.
    -   Saw blades on poles with spinning teeth at varying heights.
    -   Double-jump ability with visual ring indicator.
    -   Multi-layer parallax background: scrolling city silhouettes, star streaks at high speed, and perspective ground lines.
    -   Particle effects: jump dust, airborne trail, ramp launch sparks, score sparkle, and death explosion.
    -   Player rotation while airborne with squash/stretch on landing.
    -   Color theme progression every 10 points through 6 palettes.
    -   Speed ramps with milestone flash effects; speed capped for fairness.
    -   High score tracking. SETTINGS to start/retry, NEXT to jump, BACK to pause/quit.
-   **Pomodoro Timer**:
    -   Configurable work/break intervals (default 25m/5m).
    -   Visual progress arc (red for work, green for break).
    -   Auto-transitions between work and break with distinct melodies.
    -   NEXT to start/stop, BACK to reset, SETTINGS to configure.
    -   Long-press NEXT to navigate to next screen, long-press BACK to exit to watch face.
-   **Audio Feedback**:
    -   Gentle startup melody (ascending G major arpeggio).
    -   Unique chimes for each button action.
    -   Continuous Space Invaders-style alarm melody (looping march + urgency phrases).
    -   Pomodoro break start (descending) and break end (ascending) melodies.
    -   Game: jump blip, death crash sound, score chime.
-   **Persistent Settings**:
    -   Alarm configuration, game high score, and Pomodoro intervals are saved to non-volatile storage (ESP32 NVS) and survive power cycles.
    -   Settings save automatically when exiting configuration screens or setting a new high score.
-   **OTA Updates**:
    -   **ArduinoOTA**: Flash over WiFi from PlatformIO — `pio run -e ota -t upload --upload-port <IP>`.
    -   **ElegantOTA**: Upload firmware via any web browser at `http://<IP>/update`.
    -   Progress bar displayed on watch during OTA upload.
    -   Deep-sleep reboot after OTA for clean peripheral reset (avoids SPI crash on ESP32-C3 software reset).
    -   Custom OTA-optimized partition table with ~1.9 MB per app slot.
    -   **Rollback safety**: 30-second boot guard auto-rolls back on failed firmware. Hold BACK + NEXT during boot for manual rollback. 3-button emergency reboot (hold all 3 buttons for 2s).
    -   **Info screen**: Long-press SETTINGS on clock face to view firmware version, IP address, OTA URL, and free memory.
-   **Connectivity**:
    -   Automatic WiFi connection for NTP time synchronization and weather fetching.
    -   Dynamic updates for weather data every 10 minutes.

## 🛠️ Hardware Requirements

-   **Microcontroller**: ESP32-C3 SuperMini (or any ESP32-C3 dev board)
-   **Display**: GC9A01 1.28" Round TFT LCD (240x240, SPI)
-   **Buttons**: 3x Tactile push buttons (Back, Settings, Next)
-   **Buzzer**: 1x Passive piezo buzzer
-   **Power**: LiPo battery (optional) or USB-C

### Wiring Diagram

| Component | Pin (ESP32-C3) | Function |
| :--- | :--- | :--- |
| **GC9A01 SCK** | GPIO 5 | SPI Clock |
| **GC9A01 MOSI** | GPIO 6 | SPI Data |
| **GC9A01 DC** | GPIO 7 | Data/Command |
| **GC9A01 CS** | GPIO 8 | Chip Select |
| **GC9A01 RST** | GPIO 9 | Reset |
| **Button BACK** | GPIO 0 | Pull-up, active LOW |
| **Button SETTINGS** | GPIO 1 | Pull-up, active LOW |
| **Button NEXT** | GPIO 2 | Pull-up, active LOW |
| **Buzzer** | GPIO 3 | PWM Audio (RTC GPIO — stays silent during boot/flash) |

## 💻 Software & Libraries

This project is developed using **PlatformIO** and the **Arduino** framework.

### Dependencies
-   [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) - Optimized display driver.
-   [ArduinoJson](https://arduinojson.org/) - For parsing weather data.
-   [ElegantOTA](https://github.com/ayushsharma82/ElegantOTA) - Web-based OTA firmware updates.

### Configuration
The display and SPI pins are configured via `build_flags` in `platformio.ini`. No manual `User_Setup.h` editing is required if using the provided configuration.

## ⚙️ Setup

1.  **Clone the repository**.
2.  **Configure WiFi**: Create an `include/secrets.h` file with your credentials:
    ```cpp
    #ifndef SECRETS_H
    #define SECRETS_H
    const char* const WIFI_SSID     = "Your_SSID";
    const char* const WIFI_PASSWORD = "Your_Password";
    #endif
    ```
3.  **Location**: Update the `LATITUDE` and `LONGITUDE` constants in `include/config.h` for your local weather.
4.  **Flash**: Connect your ESP32-C3 and run:
    ```bash
    pio run -t upload
    ```
    > **Note**: The first flash after cloning (or after changing `partitions.csv`) must be via USB so the new partition table is written. Subsequent updates can use OTA.

## 🎮 Controls

-   **NEXT**: Cycle forward through screens: Watch → Weather → Timer (Clock → Alarm → Stopwatch) → Game → Pomodoro → Watch. Adjusts values in settings. Jumps in Geometry Dash. Snoozes alarm when ringing.
-   **SETTINGS**: Enter/edit alarm settings (hour, minute, on/off, snooze duration), start/stop the stopwatch (long-press to reset), configure Pomodoro, start/retry Geometry Dash, or set time/date manually on the watch face (when WiFi is disconnected).
-   **BACK**: Navigate to the previous screen, reset Pomodoro, pause/quit the game, stop the alarm, or exit settings. Long-press (1s) to exit Pomodoro to the watch face.
-   **Long-press NEXT** (Pomodoro): Navigate to next screen without starting the timer.

## 📁 Project Structure

```
include/
  config.h           — Pin definitions, colors, timing constants, location
  globals.h          — Screen enum, shared display object externs
  secrets.h          — WiFi credentials (not in repo)
src/
  main.cpp           — setup() and loop() entry points
  globals.cpp        — Display objects, drawing helpers
  buttons.h/.cpp     — Button struct, debounce logic
  buzzer.h/.cpp      — Buzzer tones and chime functions
  hijri.h/.cpp       — Gregorian to Hijri calendar conversion
  weather.h/.cpp     — Weather data, Open-Meteo API fetch, weather icons
  wifi_ntp.h/.cpp    — WiFi connection and NTP time sync
  storage.h/.cpp     — NVS persistence for user settings
  ota.h/.cpp         — ArduinoOTA and ElegantOTA setup, rollback, boot validation
partitions.csv        — Custom OTA partition table (dual 1.9 MB app slots)
  screen_info.h/.cpp — Info overlay (version, IP, OTA URL, memory)
  screen_clock.h/.cpp    — Watch face screen
  screen_weather.h/.cpp  — Weather dashboard screen
  screen_timer.h/.cpp    — Analog clock, alarm, and stopwatch screen
  screen_game.h/.cpp     — Geometry Dash mini-game
  screen_pomodoro.h/.cpp — Pomodoro timer screen
  input_handler.h/.cpp   — Button action routing for all screens
```

## 📜 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
