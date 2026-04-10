#include "ota.h"
#include "config.h"
#include "globals.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <ElegantOTA.h>

static WebServer otaServer(80);

void otaSetup() {
    if (WiFi.status() != WL_CONNECTED) return;

    // ── ArduinoOTA (PlatformIO / CLI upload) ──
    ArduinoOTA.setHostname("esp32-watch");

    ArduinoOTA.onStart([]() {
        printf("[OTA] Update starting...\n");
        // Show update screen
        spr.fillSprite(COL_BG);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(COL_CYAN, COL_BG);
        spr.drawString("OTA Update", 120, 80, 4);
        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString("Uploading...", 120, 120, 2);
        spr.pushSprite(0, 0);
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        int pct = progress * 100 / total;
        spr.fillSprite(COL_BG);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(COL_CYAN, COL_BG);
        spr.drawString("OTA Update", 120, 70, 4);

        // Progress bar
        int barW = 160;
        int barX = 120 - barW / 2;
        int barY = 110;
        spr.drawRect(barX, barY, barW, 16, COL_WHITE);
        spr.fillRect(barX + 2, barY + 2, (barW - 4) * pct / 100, 12, COL_GREEN);

        char buf[8];
        sprintf(buf, "%d%%", pct);
        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString(buf, 120, 145, 4);
        spr.pushSprite(0, 0);
    });

    ArduinoOTA.onEnd([]() {
        printf("[OTA] Update complete, rebooting.\n");
        spr.fillSprite(COL_BG);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(COL_GREEN, COL_BG);
        spr.drawString("Done!", 120, 100, 4);
        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString("Rebooting...", 120, 140, 2);
        spr.pushSprite(0, 0);
    });

    ArduinoOTA.onError([](ota_error_t error) {
        printf("[OTA] Error: %u\n", error);
        spr.fillSprite(COL_BG);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(COL_RED, COL_BG);
        spr.drawString("OTA Failed!", 120, 100, 4);
        spr.pushSprite(0, 0);
    });

    ArduinoOTA.begin();
    printf("[OTA] ArduinoOTA ready on %s\n", WiFi.localIP().toString().c_str());

    // ── ElegantOTA (web browser upload) ──
    ElegantOTA.begin(&otaServer);
    otaServer.begin();
    printf("[OTA] ElegantOTA ready at http://%s/update\n", WiFi.localIP().toString().c_str());
}

void otaLoop() {
    if (WiFi.status() != WL_CONNECTED) return;
    ArduinoOTA.handle();
    otaServer.handleClient();
}
