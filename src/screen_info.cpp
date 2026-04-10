#include "screen_info.h"
#include "config.h"
#include "globals.h"
#include <WiFi.h>

bool showInfoScreen = false;

void drawInfoScreen() {
    spr.fillSprite(COL_BG);
    drawCircleBorder();

    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString("Watch Info", 120, 28, 4);

    int y = 58;
    int spacing = 20;

    // Firmware version
    spr.setTextColor(COL_MID_GRAY, COL_BG);
    spr.drawString("Firmware", 120, y, 2);
    spr.setTextColor(COL_GREEN, COL_BG);
    spr.drawString("v" FW_VERSION, 120, y + 14, 2);
    y += spacing + 16;

    // IP address
    spr.setTextColor(COL_MID_GRAY, COL_BG);
    spr.drawString("IP Address", 120, y, 2);
    spr.setTextColor(COL_WHITE, COL_BG);
    if (WiFi.status() == WL_CONNECTED) {
        spr.drawString(WiFi.localIP().toString().c_str(), 120, y + 14, 2);
    } else {
        spr.setTextColor(COL_RED, COL_BG);
        spr.drawString("Not connected", 120, y + 14, 2);
    }
    y += spacing + 16;

    // OTA URL
    if (WiFi.status() == WL_CONNECTED) {
        spr.setTextColor(COL_MID_GRAY, COL_BG);
        spr.drawString("OTA Update", 120, y, 2);
        spr.setTextColor(COL_GOLD, COL_BG);
        char url[40];
        sprintf(url, "%s/update", WiFi.localIP().toString().c_str());
        spr.drawString(url, 120, y + 14, 2);
        y += spacing + 16;
    }

    // Free heap
    spr.setTextColor(COL_MID_GRAY, COL_BG);
    spr.drawString("Free Memory", 120, y, 2);
    spr.setTextColor(COL_WHITE, COL_BG);
    char heapBuf[16];
    sprintf(heapBuf, "%d bytes", ESP.getFreeHeap());
    spr.drawString(heapBuf, 120, y + 14, 2);

    // Footer
    spr.setTextColor(COL_DARK_GRAY, COL_BG);
    spr.drawString("Press any button to close", 120, 222, 1);

    spr.pushSprite(0, 0);
}
