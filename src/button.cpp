#include "button.h"
#include "pins.h"
#include <driver/gpio.h>
#include <esp_timer.h>

static uint32_t _press_start = 0;

void Button::init() {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BTN_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

int Button::check() {
    int level = gpio_get_level((gpio_num_t)BTN_PIN);
    uint32_t now_ms = esp_timer_get_time() / 1000;

    if (level == 0) {
        if (_press_start == 0) _press_start = now_ms;
        if (now_ms - _press_start > LONG_PRESS_MS) return 2;
    } else {
        if (_press_start > 0) {
            uint32_t elapsed = now_ms - _press_start;
            _press_start = 0;
            if (elapsed > SHORT_PRESS_MIN && elapsed < LONG_PRESS_MS)
                return 1;
        }
    }
    return 0;
}

Button button;
