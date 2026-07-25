#include "mic_i2s.h"
#include "pins.h"
#include <driver/i2s.h>
#include <math.h>

static const i2s_port_t I2S_PORT = I2S_NUM_0;

MicI2S mic;

void MicI2S::init() {
    // 延迟初始化：init 只做标记，start() 时才装 I2S 驱动
    // 避免 I2S 抢 WiFi 的 DMA/硬件资源
}

void MicI2S::start() {
    if (_running) return;

    // INMP441 I2S 配置
    // 标准 I2S Philips 格式, 8kHz, 16bit, 左声道
    // DMA 缓冲极简（2×8 样本 = 32 字节），减少与 WiFi 的 DMA 冲突
    i2s_config_t i2s_cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 8000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = 0,
        .dma_buf_count = 2,
        .dma_buf_len = 8,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_cfg = {
        .bck_io_num = MIC_SCK,    // GPIO3
        .ws_io_num = MIC_WS,      // GPIO2
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_SD     // GPIO4
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

// 读 samples 个 16bit 样本, 返回实际读到的样本数
int MicI2S::readData(int16_t *buffer, int samples) {
    if (!_running) return 0;
    size_t bytes_read = 0;
    // 超时 50ms 防止 I2S 异常时看门狗重启
    esp_err_t err = i2s_read(I2S_PORT, buffer, samples * sizeof(int16_t), &bytes_read, pdMS_TO_TICKS(50));
    if (err != ESP_OK) return 0;
    return bytes_read / sizeof(int16_t);
}
