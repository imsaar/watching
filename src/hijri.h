#ifndef HIJRI_H
#define HIJRI_H

struct HijriDate {
    int year;
    int month;
    int day;
};

extern const char* hijriMonths[];

HijriDate gregorianToHijri(int gYear, int gMonth, int gDay);

#endif
