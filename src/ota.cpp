#include "ota.h"
#include "config.h"
#include "globals.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_sleep.h>

static WebServer otaServer(80);
static bool otaUpdateDone = false;

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

    // ── ElegantOTA (web browser upload) ──
    // Uses ESP-IDF native OTA API internally (esp_ota_begin/write/end)
    ElegantOTA.begin(&otaServer);
    ElegantOTA.onEnd([](bool success) {
        if (success) {
            printf("[OTA] Update complete.\n");
            otaUpdateDone = true;
        } else {
            printf("[OTA] Update failed.\n");
        }
    });
    otaServer.begin();
    printf("[OTA] Ready at http://%s/update\n", WiFi.localIP().toString().c_str());
}

void otaLoop() {
    if (WiFi.status() != WL_CONNECTED) return;
    otaServer.handleClient();
    ElegantOTA.loop();

    if (otaUpdateDone) {
        otaUpdateDone = false;

        spr.fillSprite(COL_BG);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(COL_GREEN, COL_BG);
        spr.drawString("Done!", 120, 100, 4);
        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString("Rebooting...", 120, 140, 2);
        spr.pushSprite(0, 0);

        // Deep sleep resets all peripherals including SPI2.
        // setup() handles the partition switch on next boot.
        otaServer.stop();
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(200);
        esp_sleep_enable_timer_wakeup(1000);
        esp_deep_sleep_start();
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
