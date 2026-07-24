#include "oled_display.h"
#include "pins.h"

OLEDDisplay display;

bool OLEDDisplay::init() {
    Wire.begin(OLED_SDA, OLED_SCL);

    Wire.beginTransmission(0x3C);
    if (Wire.endTransmission() == 0) {
        _display = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
        _display->setI2CAddress(0x3C << 1);
        _display->begin();
        return true;
    }

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

void OLEDDisplay::showCentered(const String &text, int y, int size) {
    if (!_display) return;
    _display->firstPage();
    do {
        _display->setFont(size >= 2 ? u8g2_font_ncenB14_tr : u8g2_font_ncenB08_tr);
        int w = _display->getStrWidth(text.c_str());
        _display->setCursor((SCREEN_WIDTH - w) / 2, y);
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

void OLEDDisplay::showChinese(const String &text, int x, int y) {
    if (!_display) return;
    _display->firstPage();
    do {
        _display->enableUTF8Print();
        _display->setFont(u8g2_font_wqy12_t_chinese1);
        _display->setCursor(x, y);
        _display->print(text);
    } while (_display->nextPage());
}

void OLEDDisplay::chineseCentered(const String &text, int y) {
    if (!_display) return;
    _display->firstPage();
    do {
        _display->enableUTF8Print();
        _display->setFont(u8g2_font_wqy12_t_chinese1);
        int w = _display->getStrWidth(text.c_str());
        _display->setCursor((SCREEN_WIDTH - w) / 2, y);
        _display->print(text);
    } while (_display->nextPage());
}

void OLEDDisplay::drawBitmap(int x, int y, int w, int h, const uint8_t *data) {
    if (!_display) return;
    _display->firstPage();
    do {
        _display->drawXBM(x, y, w, h, data);
    } while (_display->nextPage());
}

void OLEDDisplay::showMulti(const Line *lines, uint8_t count, uint8_t startY) {
    if (!_display) return;
    _display->firstPage();
    do {
        uint8_t curY = startY;
        for (uint8_t i = 0; i < count; i++) {
            const Line &l = lines[i];
            _display->setFont(l.size >= 2 ? u8g2_font_ncenB14_tr : u8g2_font_ncenB08_tr);

            const char *txt = l.text.c_str();
            int w = _display->getStrWidth(txt);

            int x = 0;
            if (l.align == 1) x = (SCREEN_WIDTH - w) / 2;
            else if (l.align == 2) x = SCREEN_WIDTH - w;

            _display->setCursor(x, curY);
            _display->print(txt);
            curY += (l.size >= 2 ? 20 : 14);
        }
    } while (_display->nextPage());
}

void OLEDDisplay::clear() {
    if (!_display) return;
    _display->clearDisplay();
}

void OLEDDisplay::bootAnimation() {
    if (!_display) return;
    // "Link Start!" 居中显示 2 秒
    _display->firstPage();
    do {
        _display->setFont(u8g2_font_ncenB14_tr);
        _display->setCursor(8, 36);
        _display->print("Link Start!");
    } while (_display->nextPage());
    delay(2000);
}

void OLEDDisplay::displayOn() {
    if (_display) _display->setPowerSave(0);
}

void OLEDDisplay::displayOff() {
    if (_display) _display->setPowerSave(1);
}
