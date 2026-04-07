#include "globals.h"
#include "config.h"

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);
Screen currentScreen = SCREEN_CLOCK;

void drawCircleBorder() {
    spr.drawCircle(120, 120, 118, COL_DARK_GRAY);
    spr.drawCircle(120, 120, 119, COL_DARK_GRAY);
}

void drawScreenIndicator(int active) {
    int count = SCREEN_COUNT;
    int startX = 120 - (count - 1) * 6;
    for (int i = 0; i < count; i++) {
        int x = startX + i * 12;
        int y = 228;
        if (i == active) {
            spr.fillCircle(x, y, 4, COL_CYAN);
        } else {
            spr.drawCircle(x, y, 3, COL_DARK_GRAY);
        }
    }
}

int daysInMonth(int month, int year) {
    const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return d[month - 1];
}
