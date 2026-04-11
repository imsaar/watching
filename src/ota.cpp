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

    // ── Custom /update page with device info ──
    // Register BEFORE ElegantOTA to override its default /update page
    otaServer.on("/update", HTTP_GET, []() {
        const esp_partition_t* running = esp_ota_get_running_partition();
        String page = "<!DOCTYPE html><html><head><title>ESP32 Watch OTA</title>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<style>"
            "body{font-family:-apple-system,sans-serif;max-width:480px;margin:0 auto;padding:20px;background:#1a1a2e;color:#e0e0e0}"
            "h1{color:#00d4ff;text-align:center;margin-bottom:5px}"
            ".ver{text-align:center;color:#888;margin-bottom:20px}"
            "table{width:100%;border-collapse:collapse;margin-bottom:20px}"
            "td{padding:10px 8px;border-bottom:1px solid #333}"
            "td:first-child{color:#888;width:40%}"
            ".upload{background:#16213e;border:2px dashed #00d4ff;border-radius:12px;padding:30px;text-align:center;margin-top:20px}"
            "input[type=file]{margin:15px 0}"
            "button{background:#00d4ff;color:#1a1a2e;border:none;padding:12px 32px;border-radius:8px;font-size:16px;font-weight:bold;cursor:pointer}"
            "button:hover{background:#00b8d4}"
            ".bar{width:100%;height:20px;background:#333;border-radius:10px;margin-top:15px;display:none}"
            ".bar div{height:100%;background:#00d4ff;border-radius:10px;width:0%;transition:width 0.3s}"
            ".msg{text-align:center;margin-top:10px;color:#00d4ff;display:none}"
            "</style></head><body>"
            "<h1>ESP32-C3 Watch</h1>"
            "<div class='ver'>v" FW_VERSION "</div>"
            "<table>"
            "<tr><td>Hardware</td><td>ESP32-C3 SuperMini</td></tr>"
            "<tr><td>Firmware</td><td>v" FW_VERSION "</td></tr>"
            "<tr><td>Partition</td><td>" + String(running ? running->label : "?") + "</td></tr>"
            "<tr><td>IP Address</td><td>" + WiFi.localIP().toString() + "</td></tr>"
            "<tr><td>Free Heap</td><td>" + String(ESP.getFreeHeap() / 1024) + " KB</td></tr>"
            "</table>"
            "<div class='upload'>"
            "<div>Select firmware .bin file</div>"
            "<form id='f'>"
            "<input type='file' id='fw' accept='.bin'><br>"
            "<button type='submit'>Upload Firmware</button>"
            "</form>"
            "<div class='bar' id='bar'><div id='p'></div></div>"
            "<div class='msg' id='msg'></div>"
            "</div>"
            "<script>"
            "document.getElementById('f').onsubmit=async function(e){"
            "e.preventDefault();"
            "var f=document.getElementById('fw').files[0];"
            "if(!f)return;"
            "await fetch('/ota/start?mode=firmware');"
            "var d=new FormData();d.append('firmware',f);"
            "var x=new XMLHttpRequest();"
            "document.getElementById('bar').style.display='block';"
            "x.upload.onprogress=function(e){if(e.lengthComputable)document.getElementById('p').style.width=(e.loaded/e.total*100)+'%'};"
            "x.onload=function(){"
            "var m=document.getElementById('msg');"
            "m.style.display='block';"
            "if(x.status==200){m.textContent='Update successful! Rebooting...';m.style.color='#00ff88'}else{m.textContent='Update failed!';m.style.color='#ff4444'}};"
            "x.open('POST','/ota/upload');"
            "x.send(d);"
            "};"
            "</script></body></html>";
        otaServer.send(200, "text/html", page);
    });

    // ── ElegantOTA backend (handles /ota/start and /ota/upload) ──
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
    printf("[OTA] Ready at http://%s\n", WiFi.localIP().toString().c_str());
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
