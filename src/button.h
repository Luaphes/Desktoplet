#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

class Button {
public:
    void init();
    // 返回: 0=无操作, 1=短按, 2=长按
    int check();
private:
    unsigned long _pressStart = 0;
    bool _wasPressed = false;
    unsigned long _lastDebounce = 0;
    bool _lastState = HIGH;
};

extern Button button;

#endif
