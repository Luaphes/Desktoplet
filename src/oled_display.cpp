#include "oled_display.h"
#include "pins.h"
#include <U8g2lib.h>

OLEDDisplay display;

bool OLEDDisplay::init() {
    Wire.begin(OLED_SDA, OLED_SCL);

    // 先试 0x3C
    Wire.beginTransmission(0x3C);
    if (Wire.endTransmission() == 0) {
        _display = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
        _display->setI2CAddress(0x3C << 1);
        _display->begin();
        return true;
    }

    // 再试 0x3D
    Wire.beginTransmission(0x3D);
    if (Wire.endTransmission() == 0) {
        _display = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
        _display->setI2CAddress(0x3D << 1);
        _display->begin();
        return true;
    }

    return false;
}

void OLEDDisplay::showText(const String &text, int x, int y, int size) {
    if (!_display) return;
    _display->firstPage();
    do {
        _display->setFont(u8g2_font_ncenB08_tr);
        _display->setCursor(x, y);
        _display->print(text);
    } while (_display->nextPage());
}

void OLEDDisplay::showStatus(const String &line1, const String &line2) {
    if (!_display) return;
    _display->firstPage();
    do {
        _display->setFont(u8g2_font_ncenB08_tr);
        _display->setCursor(0, 12);
        _display->println(line1);
        if (line2.length() > 0) {
            _display->setCursor(0, 28);
            _display->println(line2);
        }
    } while (_display->nextPage());
}

void OLEDDisplay::showCentered(const String &text, int y, int size) {
    if (!_display) return;
    _display->firstPage();
    do {
        _display->setFont(u8g2_font_ncenB08_tr);
        int w = _display->getStrWidth(text.c_str());
        int x = (SCREEN_WIDTH - w) / 2;
        _display->setCursor(x, y);
        _display->print(text);
    } while (_display->nextPage());
}

void OLEDDisplay::clear() {
    if (!_display) return;
    _display->clearDisplay();
}

void OLEDDisplay::bootAnimation() {
    if (!_display) return;
    String msg = "HERMES";
    for (int i = 0; i <= msg.length(); i++) {
        _display->firstPage();
        do {
            _display->setFont(u8g2_font_ncenB14_tr);
            _display->setCursor(20, 36);
            _display->print(msg.substring(0, i));
        } while (_display->nextPage());
        delay(200);
    }
    delay(500);
}
