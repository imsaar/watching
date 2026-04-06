# ESP32-C3 SuperMini Smart Watch

A functional and stylish smart watch built with an ESP32-C3 SuperMini and a round GC9A01 display. This project features a multi-screen interface, real-time weather updates, Hijri calendar support, and a complete timer/alarm system.

## 🚀 Features

-   **Watch Face**:
    -   12-hour/24-hour time with a smooth seconds arc.
    -   Gregorian and Islamic (Hijri) date display.
    -   WiFi connectivity and current weather summary.
-   **Weather Dashboard**:
    -   Current temperature, "feels like", humidity, and wind speed.
    -   3-day forecast with daily highs/lows and conditions.
    -   Powered by the Open-Meteo API (no API key required).
-   **Timer & Utilities**:
    -   **Clock**: Precise NTP-synced digital clock with a visual tick-mark bezel.
    -   **Alarm**: Configurable alarm with an interactive settings menu and buzzer chime.
    -   **Stopwatch**: High-precision stopwatch with millisecond display and start/stop/reset controls.
-   **Audio Feedback**:
    -   Unique chimes for each button action.
    -   Alert melody for the alarm.
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
| **Buzzer** | GPIO 21 | PWM Audio |

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

-   **NEXT**: Switch between main screens (Watch → Weather → Timer) or cycle through sub-modes/settings.
-   **SETTINGS**: Enter/exit settings for the Alarm or Start/Stop the Stopwatch.
-   **BACK**: Go back to the main clock or reset the stopwatch.

## 📜 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
