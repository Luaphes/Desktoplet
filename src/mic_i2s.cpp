#include "mic_i2s.h"
#include "pins.h"
#include <driver/i2s.h>

static const i2s_port_t I2S_PORT = I2S_NUM_0;

// ESP32-C3 I2S0 寄存器（不依赖 soc/i2s_struct.h，那个有 volatile bug）
// 基址 0x60042000
#define I2S_CONF    (*(volatile uint32_t *)(0x60042000))
#define I2S_STATUS  (*(volatile uint32_t *)(0x6004202C))  // status 寄存器（含 rx_fifo_cnt）
#define I2S_FIFO_RD (*(volatile uint32_t *)(0x60042040))  // RX FIFO 读端口

MicI2S mic;

void MicI2S::init() {}

void MicI2S::start() {
    if (_running) return;

    // I2S 配置：不分配 DMA，只配时钟和引脚矩阵
    i2s_config_t i2s_cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = 0,
        .dma_buf_count = 0,
        .dma_buf_len = 0,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_cfg = {
        .bck_io_num = MIC_SCK,
        .ws_io_num = MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_SD
    };

    // 安装驱动（配置时钟分频器、GPIO 矩阵，不启动 DMA）
    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_cfg, 0, NULL);
    if (err != ESP_OK) return;

    err = i2s_set_pin(I2S_PORT, &pin_cfg);
    if (err != ESP_OK) {
        i2s_driver_uninstall(I2S_PORT);
        return;
    }

    // 手动启动 I2S RX 状态机（不走 DMA，直接硬启）
    i2s_start(I2S_PORT);

    _running = true;
}

void MicI2S::stop() {
    if (_running) {
        i2s_stop(I2S_PORT);
        i2s_driver_uninstall(I2S_PORT);
        _running = false;
    }
}

bool MicI2S::isRunning() {
    return _running;
}

// 轮询 I2S 内部 FIFO，不走 DMA，不跟 WiFi 抢 GDMA 通道
int MicI2S::readData(int16_t *buffer, int samples) {
    if (!_running) return 0;

    int count = 0;
    while (count < samples) {
        // status 寄存器低 6 位 = rx_fifo_cnt（RX FIFO 中 32-bit 字数）
        if ((I2S_STATUS & 0x3F) > 0) {
            buffer[count++] = (int16_t)(I2S_FIFO_RD & 0xFFFF);
        } else {
            delayMicroseconds(50);
        }
    }
    return count;
}
