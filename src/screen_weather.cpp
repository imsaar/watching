#include "screen_weather.h"
#include "config.h"
#include "globals.h"
#include "weather.h"

void drawWeatherScreen() {
    spr.fillSprite(COL_BG);
    drawCircleBorder();
    drawScreenIndicator(1);

    spr.setTextDatum(MC_DATUM);

    if (!weatherValid) {
        spr.setTextColor(COL_YELLOW, COL_BG);
        spr.drawString("Fetching", 120, 100, 4);
        spr.drawString("Weather...", 120, 130, 4);
        spr.pushSprite(0, 0);
        return;
    }

    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString("Lynnwood, WA", 120, 20, 2);

    char tempNum[8];
    sprintf(tempNum, "%.0f", currentWeather.temp);
    int numW = spr.textWidth(tempNum, 6);
    int unitW = spr.textWidth("F", 4);
    int tempTotalW = numW + 2 + unitW;
    int tempStartX = 120 - tempTotalW / 2;
    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(COL_WHITE, COL_BG);
    spr.drawString(tempNum, tempStartX, 52, 6);
    spr.setTextColor(COL_MID_GRAY, COL_BG);
    spr.drawString("F", tempStartX + numW + 2, 52, 4);
    spr.setTextDatum(MC_DATUM);

    const char* condText = wmoDescription(currentWeather.weatherCode);
    int textW = spr.textWidth(condText, 2);
    int iconW = 20;
    int totalW = iconW + 4 + textW;
    int startX = 120 - totalW / 2;
    drawWeatherIcon(startX + iconW / 2, 82, currentWeather.weatherCode, 2);
    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(wmoColor(currentWeather.weatherCode), COL_BG);
    spr.drawString(condText, startX + iconW + 4, 82, 2);
    spr.setTextDatum(MC_DATUM);

    char detailBuf[30];
    sprintf(detailBuf, "Feels %.0f°  Hum %d%%", currentWeather.feelsLike, currentWeather.humidity);
    spr.setTextColor(COL_MID_GRAY, COL_BG);
    spr.drawString(detailBuf, 120, 100, 2);

    sprintf(detailBuf, "Wind %.0f mph", currentWeather.windSpeed);
    spr.drawString(detailBuf, 120, 115, 2);

    spr.drawFastHLine(30, 130, 180, COL_DARK_GRAY);

    spr.setTextColor(COL_YELLOW, COL_BG);
    spr.drawString("3-Day Forecast", 120, 142, 2);

    for (int i = 0; i < 3; i++) {
        int y = 162 + i * 24;

        spr.setTextDatum(ML_DATUM);
        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString(forecast[i].dayName, 50, y, 2);

        drawWeatherIcon(120, y, forecast[i].weatherCode, 1);

        char fcBuf[15];
        sprintf(fcBuf, "%.0f/%.0f°", forecast[i].tempMax, forecast[i].tempMin);
        spr.setTextDatum(MR_DATUM);
        spr.setTextColor(COL_ORANGE, COL_BG);
        spr.drawString(fcBuf, 195, y, 2);
    }

    spr.pushSprite(0, 0);
}
