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
    // 文字
    void showText(const String &text, int x = 0, int y = 20, int size = 1);
    void showCentered(const String &text, int y = 20, int size = 1);
    void showStatus(const String &line1, const String &line2 = "");
    // 中文
    void showChinese(const String &text, int x = 0, int y = 20);
    void chineseCentered(const String &text, int y = 20);
    // 点阵图
    void drawBitmap(int x, int y, int w, int h, const uint8_t *data);
    // 多行排版
    struct Line {
        String text;
        uint8_t align; // 0=left, 1=center, 2=right
        uint8_t size;  // 1=small, 2=large
    };
    void showMulti(const Line *lines, uint8_t count, uint8_t startY = 12);
    // 基础
    void clear();
    void bootAnimation();
    void displayOn();
    void displayOff();
private:
    U8G2 *_display = nullptr;
};

extern OLEDDisplay display;

#endif
