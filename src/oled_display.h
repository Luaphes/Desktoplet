#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

class OLEDDisplay {
public:
    bool init();
    void showText(const String &text, int x = 0, int y = 20, int size = 1);
    void showStatus(const String &line1, const String &line2 = "");
    void showCentered(const String &text, int y = 20, int size = 1);
    void clear();
    void bootAnimation();
private:
    U8G2 *_display = nullptr;
};

extern OLEDDisplay display;

#endif
