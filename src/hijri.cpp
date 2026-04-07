#include "hijri.h"

const char* hijriMonths[] = {
    "Muharram", "Safar", "Rabi al-Awwal", "Rabi al-Thani",
    "Jumada al-Ula", "Jumada al-Thani", "Rajab", "Sha'ban",
    "Ramadan", "Shawwal", "Dhul Qi'dah", "Dhul Hijjah"
};

HijriDate gregorianToHijri(int gYear, int gMonth, int gDay) {
    int a = (14 - gMonth) / 12;
    int y = gYear + 4800 - a;
    int m = gMonth + 12 * a - 3;
    long jd = gDay + (153 * m + 2) / 5 + 365L * y + y / 4 - y / 100 + y / 400 - 32045;

    long l = jd - 1948440 + 10632;
    long n = (l - 1) / 10631;
    l = l - 10631 * n + 354;

    long j = ((long)(10985 - l) / 5316) * ((long)(50 * l) / 17719)
           + ((long)l / 5670) * ((long)(43 * l) / 15238);
    l = l - ((long)(30 - j) / 15) * ((long)(17719 * j) / 50)
           - ((long)j / 16) * ((long)(15238 * j) / 43) + 29;

    int hMonth = (int)(24 * l / 709);
    int hDay   = (int)(l - (long)(709 * hMonth) / 24);
    int hYear  = (int)(30 * n + j - 30);

    return {hYear, hMonth, hDay};
}
