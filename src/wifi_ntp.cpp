#include "wifi_ntp.h"
#include "config.h"
#include "globals.h"
#include "secrets.h"
#include <WiFi.h>
#include <time.h>

void connectWiFi() {
    printf("[WiFi] Starting connection...\n");
    printf("[WiFi] SSID: %s\n", WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    tft.fillScreen(COL_BG);
    tft.setTextColor(COL_CYAN, COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Connecting", 120, 110, 4);
    tft.drawString("WiFi...", 120, 140, 4);

    unsigned long startMs = millis();
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < 120000) {
        delay(500);
        attempts++;
        printf("[WiFi] Attempt %d — %lus elapsed — status: %d\n",
                      attempts, (millis() - startMs) / 1000, WiFi.status());
        tft.fillCircle(120, 170, 4, (attempts % 2) ? COL_CYAN : COL_BG);
    }

    if (WiFi.status() == WL_CONNECTED) {
        printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        printf("[WiFi] RSSI: %d dBm\n", WiFi.RSSI());
        tft.fillScreen(COL_BG);
        tft.setTextColor(COL_GREEN, COL_BG);
        tft.drawString("Connected!", 120, 120, 4);
        delay(1000);
    } else {
        printf("[WiFi] Failed after %d attempts, status: %d\n", attempts, WiFi.status());
        tft.fillScreen(COL_BG);
        tft.setTextColor(COL_RED, COL_BG);
        tft.drawString("WiFi Failed", 120, 120, 4);
        delay(2000);
    }
}

void syncTime() {
    if (WiFi.status() == WL_CONNECTED) {
        configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
        struct tm t;
        if (!getLocalTime(&t, 10000)) {
            printf("[NTP] Sync failed, defaulting to noon\n");
            struct timeval tv = { .tv_sec = 43200, .tv_usec = 0 };
            settimeofday(&tv, NULL);
        } else {
            printf("[NTP] Synced: %02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
        }
    } else {
        printf("[NTP] No WiFi, defaulting to noon\n");
        struct timeval tv = { .tv_sec = 43200, .tv_usec = 0 };
        settimeofday(&tv, NULL);
    }
}
