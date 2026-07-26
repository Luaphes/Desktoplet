#include "oled_display.h"
#include "pins.h"
#include <U8g2lib.h>
#include <string>

OLEDDisplay display;

// U8g2 SW I2C for ESP-IDF uses built-in gpio/delay callbacks
// Pins: SDA=GPIO8, SCL=GPIO9 (per wiring-guide.md)
static U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2_obj(U8G2_R0, OLED_SCL, OLED_SDA, U8X8_PIN_NONE);

bool OLEDDisplay::init() {
    _display = &u8g2_obj;
    _display->begin();
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

void OLEDDisplay::showChinese(const std::string &text, int x, int y) {
    if (!_display) return;
    _display->firstPage();
    do {
        _display->enableUTF8Print();
        _display->setFont(u8g2_font_wqy12_t_gb2312);
        _display->setCursor(x, y);
        _display->print(text.c_str());
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

void OLEDDisplay::drawBitmap(int x, int y, int w, int h, const uint8_t *data) {
    if (!_display) return;
    _display->firstPage();
    do {
        _display->drawXBM(x, y, w, h, data);
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

void OLEDDisplay::clear() {
    if (_display) _display->clearDisplay();
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
    // vTaskDelay in ESP-IDF — delay(2000) equivalent
    vTaskDelay(pdMS_TO_TICKS(2000));
}

void OLEDDisplay::displayOn() {
    if (_display) _display->setPowerSave(0);
}

void OLEDDisplay::displayOff() {
    if (_display) _display->setPowerSave(1);
}

void OLEDDisplay::showMulti(const Line *lines, uint8_t count, uint8_t startY) {
    if (!_display) return;
    _display->firstPage();
    do {
        uint8_t curY = startY;
        for (uint8_t i = 0; i < count; i++) {
            const Line &l = lines[i];
            _display->setFont(l.size >= 2 ? u8g2_font_ncenB14_tr : u8g2_font_ncenB08_tr);
            int w = _display->getStrWidth(l.text.c_str());
            int x = 0;
            if (l.align == 1) x = (SCREEN_WIDTH - w) / 2;
            else if (l.align == 2) x = SCREEN_WIDTH - w;
            _display->setCursor(x, curY);
            _display->print(l.text.c_str());
            curY += (l.size >= 2 ? 20 : 14);
        }
    } while (_display->nextPage());
}
