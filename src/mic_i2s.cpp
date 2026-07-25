#include "mic_i2s.h"
#include "pins.h"
#include <driver/i2s.h>
#include <soc/i2s_struct.h>

static const i2s_port_t I2S_PORT = I2S_NUM_0;

MicI2S mic;

void MicI2S::init() {
    // 延迟初始化，start() 时才装
}

void MicI2S::start() {
    if (_running) return;

    // I2S 配置：配时钟引脚，但不跑 DMA
    // dma_buf_count = 1, dma_buf_len = 2 是驱动的最低要求
    // readData 不走 i2s_read，而是直接轮询 FIFO 寄存器
    i2s_config_t i2s_cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = 0,
        .dma_buf_count = 1,
        .dma_buf_len = 2,    // 仅 4 字节，驱动安装所需的最小值
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

    // 安装驱动（配时钟、GPIO矩阵、复位外设，但 DMA 极小）
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

// 直接轮询 I2S RX FIFO 寄存器，不走 DMA
// 这样 I2S 和 WiFi 各用各的资源，不抢 GDMA 通道
int MicI2S::readData(int16_t *buffer, int samples) {
    if (!_running) return 0;

    int count = 0;
    while (count < samples) {
        // state_reg bits 16-20: RX FIFO 中 32-bit 字数（0-16）
        int fifo_cnt = (I2S0.state_reg >> 16) & 0x1F;
        if (fifo_cnt > 0) {
            // 从 FIFO 读一个字，取低 16 位
            buffer[count++] = (int16_t)(I2S0.data_rx_reg & 0xFFFF);
        } else {
            // 没数据时不空转，让出 CPU 给 WiFi
            delayMicroseconds(50);
        }
    }

    return count;
}
