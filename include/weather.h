#ifndef WEATHER_H
#define WEATHER_H

#include <Arduino.h>

struct WeatherData {
    float temp;
    float feelsLike;
    int humidity;
    int weatherCode;
    float windSpeed;
};

struct ForecastDay {
    String dayName;
    float tempMin;
    float tempMax;
    int weatherCode;
};

extern WeatherData currentWeather;
extern ForecastDay forecast[3];
extern bool weatherValid;

void updateWeather();
const char* wmoDescription(int code);
uint16_t wmoColor(int code);
void drawWeatherIcon(int cx, int cy, int code, int s);
void drawCloud(int cx, int cy, int r);

#endif
