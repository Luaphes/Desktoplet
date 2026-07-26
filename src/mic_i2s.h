#ifndef MIC_I2S_H
#define MIC_I2S_H

#include <stdint.h>

class MicI2S {
public:
    void init();
    void start();
    void stop();
    bool isRunning();
    int readData(int16_t *buffer, int samples);
private:
    bool _running = false;
};

extern MicI2S mic;

#endif
