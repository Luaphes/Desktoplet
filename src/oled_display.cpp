// 不带 U8g2 的 SSD1306 OLED 驱动
// 直接通过 I2C 发控制命令和数据到 SSD1306
// 只做基本显示，不依赖任何外部库

#include <stdio.h>
#include <string.h>
#include "pins.h"
#include "oled_display.h"
#include <driver/i2c.h>
#include <esp_log.h>

static const char *TAG = "OLED";
static bool _has_oled = false;

// SSD1306 命令
#define SSD1306_SETCONTRAST     0x81
#define SSD1306_DISPLAYALLON    0xA5
#define SSD1306_DISPLAYALLONOFF 0xA4
#define SSD1306_NORMALDISPLAY   0xA6
#define SSD1306_INVERTDISPLAY   0xA7
#define SSD1306_DISPLAYOFF      0xAE
#define SSD1306_DISPLAYON       0xAF
#define SSD1306_SETDISPLAYOFFSET 0xD3
#define SSD1306_SETCOMPINS      0xDA
#define SSD1306_SETVCOMDETECT   0xDB
#define SSD1306_SETDISPLAYCLOCKDIV 0xD5
#define SSD1306_SETPRECHARGE    0xD9
#define SSD1306_SETMULTIPLEX    0xA8
#define SSD1306_SETLOWCOLUMN    0x00
#define SSD1306_SETHIGHCOLUMN   0x10
#define SSD1306_SETSTARTLINE    0x40
#define SSD1306_MEMORYMODE      0x20
#define SSD1306_COLUMNADDR      0x21
#define SSD1306_PAGEADDR        0x22
#define SSD1306_COMSCANINC      0xC0
#define SSD1306_COMSCANDEC      0xC8
#define SSD1306_SEGREMAP        0xA0
#define SSD1306_CHARGEPUMP      0x8D
#define SSD1306_EXTERNALVCC     0x01
#define SSD1306_SWITCHCAPVCC    0x02

#define OLED_I2C_ADDR 0x3C
#define OLED_WIDTH    128
#define OLED_HEIGHT   64
#define OLED_PAGES    8

static uint8_t _fb[OLED_WIDTH * OLED_PAGES]; // 1024 bytes

static void i2c_write_cmd(uint8_t cmd) {
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, (OLED_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(c, 0x00, true); // command mode
    i2c_master_write_byte(c, cmd, true);
    i2c_master_stop(c);
    i2c_master_cmd_begin(I2C_NUM_0, c, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(c);
}

static void i2c_write_data(const uint8_t *data, int len) {
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, (OLED_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(c, 0x40, true); // data mode
    i2c_master_write(c, (uint8_t *)data, len, true);
    i2c_master_stop(c);
    i2c_master_cmd_begin(I2C_NUM_0, c, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(c);
}

void oled_init() {
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_SDA,
        .scl_io_num = OLED_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    if (i2c_param_config(I2C_NUM_0, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "I2C config failed");
        return;
    }
    if (i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) {
        ESP_LOGE(TAG, "I2C install failed");
        return;
    }
    _has_oled = true;

    // Init sequence
    i2c_write_cmd(0xAE); // display off
    i2c_write_cmd(0x20); i2c_write_cmd(0x00); // horizontal addr mode
    i2c_write_cmd(0xB0); // page start
    i2c_write_cmd(0xC8); // COM scan direction
    i2c_write_cmd(0x00); // low column
    i2c_write_cmd(0x10); // high column
    i2c_write_cmd(0x40); // start line
    i2c_write_cmd(0x81); i2c_write_cmd(0x7F); // contrast
    i2c_write_cmd(0xA1); // segment remap
    i2c_write_cmd(0xA6); // normal display
    i2c_write_cmd(0xA8); i2c_write_cmd(0x3F); // mux ratio
    i2c_write_cmd(0xA4); // display on resume
    i2c_write_cmd(0xD3); i2c_write_cmd(0x00); // display offset
    i2c_write_cmd(0xD5); i2c_write_cmd(0x80); // clock divide
    i2c_write_cmd(0xD9); i2c_write_cmd(0x22); // pre-charge
    i2c_write_cmd(0xDA); i2c_write_cmd(0x12); // COM pins
    i2c_write_cmd(0xDB); i2c_write_cmd(0x20); // VCOM detect
    i2c_write_cmd(0x8D); i2c_write_cmd(0x14); // charge pump
    i2c_write_cmd(0xAF); // display on
}

void oled_clear() {
    memset(_fb, 0, sizeof(_fb));
    oled_flush();
}

void oled_flush() {
    if (!_has_oled) return;
    i2c_write_cmd(0x21); i2c_write_cmd(0); i2c_write_cmd(127); // column range
    i2c_write_cmd(0x22); i2c_write_cmd(0); i2c_write_cmd(7);   // page range
    i2c_write_data(_fb, sizeof(_fb));
}

void oled_draw_char(int x, int y, char c) {
    // 8x8 基本字库
    static const uint8_t font8x8[256][8] = {
        {' ',' ',' ',' ',' ',' ',' ',' '},
        // A-Z 小写和大写共用
        ['A']={0x00,0x00,0x7C,0x12,0x12,0x12,0x7C,0x00},
        ['a']={0x00,0x00,0x7C,0x12,0x12,0x12,0x7C,0x00},
        // 更多字符
    };
    // 临时简单点阵
}

void oled_text(int x, int y, const char *text) {
    // 简版
}
