#include "ota.h"
#include "config.h"
#include "globals.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

static WebServer otaServer(80);
static bool elegantOtaDone = false;

// Log partition table state for debugging
static void logPartitions() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* boot    = esp_ota_get_boot_partition();
    const esp_partition_t* update  = esp_ota_get_next_update_partition(NULL);

    printf("[OTA] Running partition: %s (0x%06x, %d bytes)\n",
           running ? running->label : "?",
           running ? running->address : 0,
           running ? running->size : 0);
    printf("[OTA] Boot partition:    %s (0x%06x)\n",
           boot ? boot->label : "?",
           boot ? boot->address : 0);
    printf("[OTA] Next OTA target:   %s (0x%06x, %d bytes)\n",
           update ? update->label : "NONE",
           update ? update->address : 0,
           update ? update->size : 0);
}

void otaSetup() {
    if (WiFi.status() != WL_CONNECTED) return;

    logPartitions();

    // ── ArduinoOTA (PlatformIO / CLI upload) ──
    ArduinoOTA.setHostname("esp32-watch");

    ArduinoOTA.onStart([]() {
        printf("[OTA] ArduinoOTA update starting...\n");
        const esp_partition_t* target = esp_ota_get_next_update_partition(NULL);
        printf("[OTA] Writing to partition: %s (0x%06x)\n",
               target ? target->label : "UNKNOWN",
               target ? target->address : 0);

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
        printf("[OTA] Upload finished. Verifying...\n");

        // Log post-write state
        const esp_partition_t* boot = esp_ota_get_boot_partition();
        printf("[OTA] Boot partition is now: %s (0x%06x)\n",
               boot ? boot->label : "?", boot ? boot->address : 0);

        // Verify the new partition has a valid image header
        const esp_partition_t* target = esp_ota_get_next_update_partition(esp_ota_get_running_partition());
        if (target) {
            esp_app_desc_t desc;
            esp_err_t err = esp_ota_get_partition_description(target, &desc);
            if (err == ESP_OK) {
                printf("[OTA] Verified: %s has valid app (v%s)\n", target->label, desc.version);
            } else {
                printf("[OTA] WARNING: %s image verification failed! err=0x%x\n", target->label, err);
            }
        }

        spr.fillSprite(COL_BG);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(COL_GREEN, COL_BG);
        spr.drawString("Done!", 120, 100, 4);
        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString("Rebooting...", 120, 140, 2);
        spr.pushSprite(0, 0);
        delay(1000);
        ESP.restart();
    });

    ArduinoOTA.onError([](ota_error_t error) {
        const char* errStr = "Unknown";
        switch (error) {
            case OTA_AUTH_ERROR:    errStr = "Auth Failed"; break;
            case OTA_BEGIN_ERROR:   errStr = "Begin Failed"; break;
            case OTA_CONNECT_ERROR: errStr = "Connect Failed"; break;
            case OTA_RECEIVE_ERROR: errStr = "Receive Failed"; break;
            case OTA_END_ERROR:     errStr = "End Failed"; break;
        }
        printf("[OTA] Error %u: %s\n", error, errStr);
        spr.fillSprite(COL_BG);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(COL_RED, COL_BG);
        char msg[32];
        sprintf(msg, "OTA: %s", errStr);
        spr.drawString(msg, 120, 100, 4);
        spr.pushSprite(0, 0);
    });

    ArduinoOTA.begin();
    printf("[OTA] ArduinoOTA ready on %s\n", WiFi.localIP().toString().c_str());

    // ── ElegantOTA (web browser upload) ──
    ElegantOTA.begin(&otaServer);
    ElegantOTA.onEnd([](bool success) {
        if (success) {
            printf("[OTA] ElegantOTA update complete.\n");
            elegantOtaDone = true;
        } else {
            printf("[OTA] ElegantOTA update failed.\n");
        }
    });
    otaServer.begin();
    printf("[OTA] ElegantOTA ready at http://%s/update\n", WiFi.localIP().toString().c_str());
}

void otaLoop() {
    if (WiFi.status() != WL_CONNECTED) return;
    ArduinoOTA.handle();
    otaServer.handleClient();
    ElegantOTA.loop();

    if (elegantOtaDone) {
        elegantOtaDone = false;
        spr.fillSprite(COL_BG);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(COL_GREEN, COL_BG);
        spr.drawString("Done!", 120, 100, 4);
        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString("Rebooting...", 120, 140, 2);
        spr.pushSprite(0, 0);
        delay(1000);
        ESP.restart();
    }
}

void otaValidateApp() {
    esp_ota_mark_app_valid_cancel_rollback();

    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running) {
        printf("[OTA] Running from partition: %s (addr: 0x%06x)\n",
               running->label, running->address);
    }
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    if (boot) {
        printf("[OTA] Boot partition: %s (addr: 0x%06x)\n",
               boot->label, boot->address);
    }
}

bool otaRollback() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* other = esp_ota_get_next_update_partition(NULL);

    if (!other || other == running) {
        printf("[OTA] No rollback partition available\n");
        return false;
    }

    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(other, &desc) != ESP_OK) {
        printf("[OTA] Rollback partition has no valid app\n");
        return false;
    }

    printf("[OTA] Rolling back to partition %s (v%s)\n", other->label, desc.version);

    if (esp_ota_set_boot_partition(other) != ESP_OK) {
        printf("[OTA] Failed to set boot partition\n");
        return false;
    }

    return true;
}
