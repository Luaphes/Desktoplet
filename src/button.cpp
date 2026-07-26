#include "button.h"
#include "pins.h"

Button button;

void Button::init() {
    pinMode(BTN_PIN, INPUT_PULLUP);
}

int Button::check() {
    bool current = digitalRead(BTN_PIN);
    unsigned long now = millis();

    // 防抖
    if (current != _lastState) {
        _lastDebounce = now;
        _lastState = current;
        if (current == LOW) {
            _pressStart = now;
            _wasPressed = true;
        } else {
            // 松手，判断按了多久
            unsigned long duration = now - _pressStart;
            _pressStart = 0;
            _wasPressed = false;
            if (duration >= LONG_PRESS_MS) {
                return 2; // 长按
            } else if (duration >= DEBOUNCE_MS) {
                return 1; // 短按
            }
        }
        return 0;
    }

    // 还在按住中，检查是否已达到长按阈值
    if (_wasPressed && current == LOW && (now - _pressStart >= LONG_PRESS_MS)) {
        // 已判定长按，等待松手后才触发（上面松手分支会返回2）
        // 但为了防止死按，超过阈值后如果还在按，返回2
        _wasPressed = false; // 防止重复触发
        return 2;
    }

    return 0;
}
