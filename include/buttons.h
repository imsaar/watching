#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>

struct Button {
    uint8_t pin;
    bool lastState;
    bool pressed;
    unsigned long lastDebounce;
    unsigned long holdStart;
    bool holding;
    bool holdFired;
};

extern Button buttons[3];

void updateButtons();

#endif
