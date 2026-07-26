#include "mic_i2s.h"
#include "pins.h"
#include <driver/i2s.h>

static const i2s_port_t I2S_PORT = I2S_NUM_0;

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
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 2,
        .dma_buf_len = 8,
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
    _running = false;
    // 不卸驱动——卸了必炸 WiFi
}

bool MicI2S::isRunning() {
    return _running;
}

int MicI2S::readData(int16_t *buffer, int samples) {
    if (!_running) return 0;

    // 读前启动 I2S（启用 DMA 传输）
    i2s_start(I2S_PORT);

    size_t bytes_read = 0;
    int to_read = (samples > 4) ? 4 : samples;
    esp_err_t err = i2s_read(I2S_PORT, buffer, to_read * sizeof(int16_t), &bytes_read, pdMS_TO_TICKS(10));

    // 读后停掉 I2S（暂停 DMA，让 WiFi 独占总线）
    i2s_stop(I2S_PORT);

    if (err != ESP_OK) return 0;
    return bytes_read / sizeof(int16_t);
}
