#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <string>
#include <U8g2lib.h>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

class OLEDDisplay {
public:
    bool init();
    void showCentered(const std::string &text, int y = 20, int size = 1);
    void showStatus(const std::string &line1, const std::string &line2 = "");
    void chineseCentered(const std::string &text, int y = 20);
    void drawVolumeBar(int level);
    void bootAnimation();
    void clear();
private:
    U8G2 *_display = nullptr;
};

extern OLEDDisplay display;

#endif
