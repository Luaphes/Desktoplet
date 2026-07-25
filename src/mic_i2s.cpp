#include "mic_i2s.h"
#include "pins.h"
#include <driver/i2s.h>

static const i2s_port_t I2S_PORT = I2S_NUM_0;

// ESP32-C3 I2S0 寄存器偏移（不依赖任何头文件结构体）
#define I2S0_BASE      0x60042000
#define I2S0_STATUS    (*(volatile uint32_t *)(I2S0_BASE + 0x04))
#define I2S0_RX_FIFO   (*(volatile uint32_t *)(I2S0_BASE + 0x40))

MicI2S mic;

void MicI2S::init() {}

void MicI2S::start() {
    if (_running) return;

    i2s_config_t i2s_cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = 0,
        .dma_buf_count = 0,
        .dma_buf_len = 0,      // 不启动 DMA，FIFO 靠寄存器轮询
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

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_cfg, 0, NULL);
    if (err != ESP_OK) return;

    err = i2s_set_pin(I2S_PORT, &pin_cfg);
    if (err != ESP_OK) {
        i2s_driver_uninstall(I2S_PORT);
        return;
    }

    _running = true;
}

void MicI2S::stop() {
    if (_running) {
        i2s_driver_uninstall(I2S_PORT);
        _running = false;
    }
}

bool MicI2S::isRunning() {
    return _running;
}

int MicI2S::readData(int16_t *buffer, int samples) {
    if (!_running) return 0;

    int count = 0;
    while (count < samples) {
        // 直接从 FIFO 读，不检查计数（C3 寄存器布局未知）
        int16_t sample = (int16_t)(I2S0_RX_FIFO & 0xFFFF);
        buffer[count++] = sample;
        delayMicroseconds(50);
    }
    return count;
}
