#include "mic_i2s.h"
#include "pins.h"
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>

static i2s_chan_handle_t rx_chan = NULL;
static const char *TAG = "I2S";

MicI2S mic;

void MicI2S::init() {}

void MicI2S::start() {
    if (rx_chan) return;

    // 1. 通道配置 (ESP-IDF 6.x: id + role only)
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
    };
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &rx_chan);
    if (err != ESP_OK || rx_chan == NULL) return;

    // 2. STD 模式配置
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)MIC_SCK,
            .ws = (gpio_num_t)MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = (gpio_num_t)MIC_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    err = i2s_channel_init_std_mode(rx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init_std failed: %d", err);
        i2s_del_channel(rx_chan);
        rx_chan = NULL;
        return;
    }

    err = i2s_channel_enable(rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "enable failed: %d", err);
        i2s_del_channel(rx_chan);
        rx_chan = NULL;
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
    esp_err_t err = i2s_channel_read(rx_chan, buffer,
        samples * sizeof(int16_t), &bytes_read, pdMS_TO_TICKS(50));
    if (err != ESP_OK) return 0;
    return bytes_read / sizeof(int16_t);
}
