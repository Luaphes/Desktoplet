#include "oled_display.h"
#include "pins.h"
#include "version.h"
#include <U8g2lib.h>
#include <driver/gpio.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

OLEDDisplay display;

// SSD1315 ≈ SH1106 compatible, 128×64, I2C addr 0x3C
// Use SH1106 constructor since SSD1315 is a SH11xx variant
static U8G2_SH1106_128X64_NONAME_F_SW_I2C u8g2_obj(U8G2_R0, OLED_SCL, OLED_SDA, U8X8_PIN_NONE);

// ESP-IDF GPIO + delay callback (replaces Arduino's digitalWrite/delayMicroseconds)
static uint8_t gpio_and_delay_esp32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch (msg) {
    case U8X8_MSG_DELAY_100NANO:
        esp_rom_delay_us(1);
        break;
    case U8X8_MSG_DELAY_10MICRO:
        esp_rom_delay_us(10);
        break;
    case U8X8_MSG_DELAY_MILLI:
        vTaskDelay(pdMS_TO_TICKS(arg_int));
        break;
    case U8X8_MSG_GPIO_I2C_CLOCK:
        gpio_set_level((gpio_num_t)OLED_SCL, arg_int);
        break;
    case U8X8_MSG_GPIO_I2C_DATA:
        gpio_set_level((gpio_num_t)OLED_SDA, arg_int);
        break;
    case U8X8_MSG_GPIO_RESET:
        break;
    default:
        u8x8_SetGPIOResult(u8x8, 1);
        break;
    }
    return 1;
}

bool OLEDDisplay::init() {
    // 初始化 GPIO 为输出（SW I2C 需要）
    gpio_set_direction((gpio_num_t)OLED_SCL, GPIO_MODE_OUTPUT_OD);
    gpio_set_direction((gpio_num_t)OLED_SDA, GPIO_MODE_OUTPUT_OD);
    gpio_set_level((gpio_num_t)OLED_SCL, 1);
    gpio_set_level((gpio_num_t)OLED_SDA, 1);

    // 替换 U8g2 的默认 Arduino 回调为 ESP-IDF 回调
    u8g2_obj.getU8x8()->gpio_and_delay_cb = gpio_and_delay_esp32;

    _display = &u8g2_obj;
    _display->begin();
    _display->setFlipMode(0);
    return true;
}

void OLEDDisplay::showCentered(const std::string &text, int y, int size) {
    if (!_display) return;
    _display->firstPage();
    do {
        _display->setFont(size >= 2 ? u8g2_font_ncenB14_tr : u8g2_font_ncenB08_tr);
        int w = _display->getStrWidth(text.c_str());
        _display->setCursor((SCREEN_WIDTH - w) / 2, y);
        _display->print(text.c_str());
    } while (_display->nextPage());
}

void OLEDDisplay::showStatus(const std::string &line1, const std::string &line2) {
    if (!_display) return;
    _display->firstPage();
    do {
        _display->setFont(u8g2_font_ncenB08_tr);
        _display->setCursor(0, 12);
        _display->println(line1.c_str());
        if (line2.length() > 0) {
            _display->setCursor(0, 28);
            _display->println(line2.c_str());
        }
    } while (_display->nextPage());
}

void OLEDDisplay::chineseCentered(const std::string &text, int y) {
    if (!_display) return;
    _display->firstPage();
    do {
        _display->enableUTF8Print();
        _display->setFont(u8g2_font_wqy12_t_gb2312);
        int w = _display->getUTF8Width(text.c_str());
        _display->setCursor((SCREEN_WIDTH - w) / 2, y);
        _display->print(text.c_str());
    } while (_display->nextPage());
}

void OLEDDisplay::drawVolumeBar(int level) {
    if (!_display) return;
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    _display->firstPage();
    do {
        _display->setFont(u8g2_font_ncenB08_tr);
        _display->setCursor(0, 12);
        _display->print("MIC Test");

        int barX = 10, barY = 24, barW = 108, barH = 16;
        _display->drawFrame(barX, barY, barW, barH);
        int fillW = (level * (barW - 4)) / 100;
        if (fillW > 0)
            _display->drawBox(barX + 2, barY + 2, fillW, barH - 4);

        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", level);
        _display->setCursor((SCREEN_WIDTH - _display->getStrWidth(pct)) / 2, 56);
        _display->print(pct);
    } while (_display->nextPage());
}

void OLEDDisplay::bootAnimation() {
    if (!_display) return;
    _display->firstPage();
    do {
        _display->setFont(u8g2_font_ncenB14_tr);
        _display->setCursor(8, 36);
        _display->print("Link Start!");
        _display->setFont(u8g2_font_ncenB08_tr);
        _display->setCursor(104, 60);
        _display->print(FIRMWARE_VERSION);
    } while (_display->nextPage());
    vTaskDelay(pdMS_TO_TICKS(1500));
}

void OLEDDisplay::clear() {
    if (_display) _display->clearDisplay();
}
