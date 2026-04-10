#ifndef CONFIG_H
#define CONFIG_H

// ── Firmware Version ────────────────────────────────────────────
#define FW_VERSION "1.1.0"

// ── Pin Definitions ──────────────────────────────────────────────
#define BTN_BACK     0
#define BTN_SETTINGS 1
#define BTN_NEXT     2
#define BUZZER_PIN   3
#define BUZZER_CH    0  // LEDC channel for buzzer

// ── Colors (RGB565) ─────────────────────────────────────────────
#define COL_BG         0x0000
#define COL_WHITE      0xFFFF
#define COL_CYAN       0x07FF
#define COL_YELLOW     0xFFE0
#define COL_ORANGE     0xFD20
#define COL_GREEN      0x07E0
#define COL_RED        0xF800
#define COL_MAGENTA    0xF81F
#define COL_BLUE       0x001F
#define COL_LIGHT_BLUE 0x653F
#define COL_DARK_GRAY  0x4208
#define COL_MID_GRAY   0x8410
#define COL_GOLD       0xFEA0
#define COL_TEAL       0x0410

// ── Timing Constants ────────────────────────────────────────────
#define DEBOUNCE_MS  200
#define HOLD_MS      1000
#define WEATHER_INTERVAL 600000  // 10 minutes

// ── Location (Lynnwood, WA) ─────────────────────────────────────
constexpr float LATITUDE  = 47.8209f;
constexpr float LONGITUDE = -122.3151f;

// ── NTP ─────────────────────────────────────────────────────────
#define NTP_SERVER  "pool.ntp.org"
#define GMT_OFFSET  (-8 * 3600)
#define DST_OFFSET  3600

#endif
