#include "buttons.h"
#include "config.h"

Button buttons[] = {
    {BTN_BACK, false, false, 0, 0, false, false},
    {BTN_SETTINGS, false, false, 0, 0, false, false},
    {BTN_NEXT, false, false, 0, 0, false, false}
};

void updateButtons() {
    unsigned long now = millis();
    for (int i = 0; i < 3; i++) {
        bool state = !digitalRead(buttons[i].pin);
        buttons[i].pressed = false;
        if (state && !buttons[i].lastState && (now - buttons[i].lastDebounce > DEBOUNCE_MS)) {
            buttons[i].pressed = true;
            buttons[i].lastDebounce = now;
            buttons[i].holdStart = now;
            buttons[i].holdFired = false;
        }
        buttons[i].holding = state;
        if (!state) buttons[i].holdFired = false;
        buttons[i].lastState = state;
    }
}
