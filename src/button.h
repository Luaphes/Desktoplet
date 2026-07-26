#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>

class Button {
public:
    void init();
    // 返回: 0=无操作, 1=短按, 2=长按
    int check();
};

extern Button button;

#endif
