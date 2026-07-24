#ifndef PINS_H
#define PINS_H

// 引脚定义（按接线图，不改固件就不动这里）
#define BTN_PIN    0
#define MIC_WS     2
#define MIC_SCK    3
#define MIC_SD     4
#define OLED_SDA   8
#define OLED_SCL   9

// OLED I2C 地址
#define OLED_ADDR  0x3C

// 按键时长定义
#define LONG_PRESS_MS    5000
#define SHORT_PRESS_MAX  1200
#define DEBOUNCE_MS      50

// WebSocket
#define WS_PORT     8765
#define WS_RECONNECT_SEC 5

// OTA
#define OTA_TIMEOUT_MS    120000
#define OTA_CHUNK_SIZE    4096

// NVS 命名空间
#define NVS_NAMESPACE     "esp32-hermes"

#endif
