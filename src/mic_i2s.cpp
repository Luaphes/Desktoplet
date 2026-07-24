#include "mic_i2s.h"
#include "pins.h"

// v0.1: MIC 模块留空占位。v1.0 时实现 I2S 驱动
// INMP441 接线: SD→GPIO4, SCK→GPIO3, WS→GPIO2, L/R→GND

MicI2S mic;

void MicI2S::init() {
    // v0.1 不初始化 I2S，节省内存
}

void MicI2S::start() {
    // v1.0 实现
}

void MicI2S::stop() {
    // v1.0 实现
}

bool MicI2S::isRunning() {
    return _running;
}

int MicI2S::readData(int16_t *buffer, int samples) {
    // v1.0 实现
    return 0;
}
