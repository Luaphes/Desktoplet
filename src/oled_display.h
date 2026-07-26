#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <U8g2lib.h>
#include <stdint.h>
#include <string>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

class OLEDDisplay {
public:
    bool init();
    void showCentered(const std::string &text, int y = 20, int size = 1);
    void showStatus(const std::string &line1, const std::string &line2 = "");
    void showChinese(const std::string &text, int x = 0, int y = 20);
    void chineseCentered(const std::string &text, int y = 20);
    void drawBitmap(int x, int y, int w, int h, const uint8_t *data);
    void drawVolumeBar(int level);
    void clear();
    void bootAnimation();
    void displayOn();
    void displayOff();
    struct Line {
        std::string text;
        uint8_t align;
        uint8_t size;
    };
    void showMulti(const Line *lines, uint8_t count, uint8_t startY = 12);
private:
    U8G2 *_display = nullptr;
};

extern OLEDDisplay display;

#endif
