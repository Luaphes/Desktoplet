#include "mic_i2s.h"
#include <driver/i2s_std.h>

static const i2s_port_t I2S_PORT = I2S_NUM_0;
static i2s_chan_handle_t rx_chan = NULL;

MicI2S mic;

void MicI2S::init() {}

void MicI2S::start() {
    if (rx_chan) return;

    // 新 I2S STD API（ESP-IDF 5.x 重写，对 C3 GDMA 处理更完善）
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = 16000,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_MONO
        ),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_3,
            .ws = GPIO_NUM_2,
            .dout = I2S_GPIO_UNUSED,
            .din = GPIO_NUM_4,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    esp_err_t err = i2s_new_channel(&std_cfg, NULL, &rx_chan);
    if (err != ESP_OK || !rx_chan) return;

    err = i2s_channel_enable(rx_chan);
    if (err != ESP_OK) {
        i2s_del_channel(rx_chan);
        rx_chan = NULL;
        return;
    }
}

void MicI2S::stop() {
    if (rx_chan) {
        i2s_channel_disable(rx_chan);
        i2s_del_channel(rx_chan);
        rx_chan = NULL;
    }
}

bool MicI2S::isRunning() {
    return rx_chan != NULL;
}

int MicI2S::readData(int16_t *buffer, int samples) {
    if (!rx_chan) return 0;

    size_t bytes_read = 0;
    size_t bytes_to_read = samples * sizeof(int16_t);
    esp_err_t err = i2s_channel_read(rx_chan, buffer, bytes_to_read, &bytes_read, pdMS_TO_TICKS(50));
    if (err != ESP_OK) return 0;
    return bytes_read / sizeof(int16_t);
}
