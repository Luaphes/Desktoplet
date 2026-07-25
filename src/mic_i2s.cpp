#include "mic_i2s.h"
#include "pins.h"
#include <driver/i2s.h>
#include <hal/i2s_ll.h>

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
        .intr_alloc_flags = 0,
        .dma_buf_count = 1,
        .dma_buf_len = 2,
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

// 轮询 I2S RX FIFO，不走 DMA
// 使用 HAL LL 函数，自动适配各芯片的寄存器布局
int MicI2S::readData(int16_t *buffer, int samples) {
    if (!_running) return 0;

    i2s_dev_t *hw = (i2s_dev_t *)I2S0_BASE_ADDR;
    int count = 0;

    while (count < samples) {
        if (i2s_ll_rx_get_fifo_cnt(hw) > 0) {
            buffer[count++] = (int16_t)(i2s_ll_rx_read_fifo(hw) & 0xFFFF);
        } else {
            delayMicroseconds(50);
        }
    }

    return count;
}
