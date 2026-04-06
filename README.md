# ESP32-C3 SuperMini Smart Watch

A functional and stylish smart watch built with an ESP32-C3 SuperMini and a round GC9A01 display. This project features a multi-screen interface, real-time weather updates, Hijri calendar support, and a complete timer/alarm system.

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
    -   **Alarm**: Configurable alarm with an interactive settings menu and buzzer chime.
    -   **Stopwatch**: High-precision stopwatch with millisecond display. Start/stop with SETTINGS button, long-press SETTINGS to reset when stopped.
-   **Pomodoro Timer**:
    -   Configurable work/break intervals (default 25m/5m).
    -   Visual progress arc (red for work, green for break).
    -   Auto-transitions between work and break with distinct melodies.
    -   NEXT to start/stop, BACK to reset, SETTINGS to configure, hold BACK to exit.
-   **Audio Feedback**:
    -   Gentle startup melody (ascending G major arpeggio).
    -   Unique chimes for each button action.
    -   Alert melody for the alarm.
    -   Pomodoro break start (descending) and break end (ascending) melodies.
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

### Configuration
The display and SPI pins are configured via `build_flags` in `platformio.ini`. No manual `User_Setup.h` editing is required if using the provided configuration.

## ⚙️ Setup

1.  **Clone the repository**.
2.  **Configure WiFi**: Create a `src/secrets.h` file with your credentials:
    ```cpp
    #define WIFI_SSID "Your_SSID"
    #define WIFI_PASSWORD "Your_Password"
    ```
3.  **Location**: Update the `LATITUDE` and `LONGITUDE` constants in `src/main.cpp` for your local weather.
4.  **Flash**: Connect your ESP32-C3 and run:
    ```bash
    pio run --target upload
    ```

## 🎮 Controls

-   **NEXT**: Cycle through all screens: Watch → Weather → Timer (Clock → Alarm → Stopwatch) → Pomodoro → Watch. Adjusts values in settings.
-   **SETTINGS**: Enter/edit alarm settings, start/stop the stopwatch (long-press to reset), configure Pomodoro, or set time/date manually on the watch face (when WiFi is disconnected).
-   **BACK**: Navigate to the previous screen, reset Pomodoro, or exit settings. Long-press (1s) to exit Pomodoro to the watch face.

## 📜 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
