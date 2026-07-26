#include "oled_display.h"
#include "pins.h"
#include "version.h"
#include <driver/gpio.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <cstdio>

OLEDDisplay display;

#define OLED_ADDR 0x3C
#define WIDTH  128
#define HEIGHT 64
#define PAGES  8

static uint8_t _fb[WIDTH * PAGES]; // 1024 bytes framebuffer

// ---- SW I2C bit-bang ----
static void i2c_delay() { esp_rom_delay_us(2); }
static void scl_low()  { gpio_set_level((gpio_num_t)OLED_SCL, 0); }
static void scl_high() { gpio_set_level((gpio_num_t)OLED_SCL, 1); }
static void sda_low()  { gpio_set_level((gpio_num_t)OLED_SDA, 0); }
static void sda_high() { gpio_set_level((gpio_num_t)OLED_SDA, 1); }

static void i2c_start() {
    sda_high(); scl_high(); i2c_delay();
    sda_low();  i2c_delay();
    scl_low();  i2c_delay();
}

static void i2c_stop() {
    sda_low();  scl_high(); i2c_delay();
    sda_high(); i2c_delay();
}

static bool i2c_write_byte(uint8_t b) {
    for (int i = 0; i < 8; i++) {
        if (b & 0x80) sda_high(); else sda_low();
        i2c_delay();
        scl_high(); i2c_delay();
        scl_low();  i2c_delay();
        b <<= 1;
    }
    // ACK
    sda_high(); i2c_delay();
    scl_high(); i2c_delay();
    bool ack = gpio_get_level((gpio_num_t)OLED_SDA) == 0;
    scl_low();  i2c_delay();
    return ack;
}

static void oled_write_cmd(uint8_t cmd) {
    i2c_start();
    i2c_write_byte(OLED_ADDR << 1);
    i2c_write_byte(0x00); // command mode
    i2c_write_byte(cmd);
    i2c_stop();
}

static void oled_write_data(const uint8_t *data, int len) {
    i2c_start();
    i2c_write_byte(OLED_ADDR << 1);
    i2c_write_byte(0x40); // data mode
    for (int i = 0; i < len; i++) i2c_write_byte(data[i]);
    i2c_stop();
}

// ---- SH1106/SSD1315 init ----
static void oled_init_seq() {
    esp_rom_delay_us(100000); // power-up delay
    oled_write_cmd(0xAE); // display off
    oled_write_cmd(0xD5); oled_write_cmd(0x90); // clock divide
    oled_write_cmd(0xA8); oled_write_cmd(0x3F); // mux ratio
    oled_write_cmd(0xD3); oled_write_cmd(0x00); // display offset
    oled_write_cmd(0x40); // start line
    oled_write_cmd(0xA1); // segment remap
    oled_write_cmd(0xC8); // COM scan reverse
    oled_write_cmd(0xDA); oled_write_cmd(0x12); // COM pins
    oled_write_cmd(0x81); oled_write_cmd(0x7F); // contrast
    oled_write_cmd(0xD9); oled_write_cmd(0x22); // pre-charge
    oled_write_cmd(0xDB); oled_write_cmd(0x30); // VCOM deselect
    oled_write_cmd(0xA4); // output RAM to display
    oled_write_cmd(0xA6); // normal (non-inverted)
    oled_write_cmd(0xAD); oled_write_cmd(0x10); // internal Iref
    oled_write_cmd(0x8D); oled_write_cmd(0x14); // charge pump
    oled_write_cmd(0xAF); // display on
}

static void oled_flush() {
    // OLED 物理倒装：framebuffer page 7 → display page 0 (物理底部)
    for (int disp_page = 0; disp_page < PAGES; disp_page++) {
        oled_write_cmd(0xB0 + disp_page); // set display page
        oled_write_cmd(0x00); // lower column start (SSD1315: col 0)
        oled_write_cmd(0x10); // upper column start
        int fb_page = PAGES - 1 - disp_page;
        oled_write_data(_fb + fb_page * WIDTH, WIDTH);
    }
}

// ---- ASCII 8x8 font (basic, for SH1106 text) ----
static const uint8_t font8x8[96][8] = {
#include "font8x8.inc"
};
// Note: full font 32-127 not included for brevity, will extend if needed

static void draw_char(int x, int y, char c, bool large) {
    if (c < ' ') c = ' ';
    int idx = (unsigned char)c - ' ';
    if (idx > 95) idx = 0;
    const uint8_t *glyph = font8x8[idx];
    for (int row = 0; row < 8; row++) {
        int screen_y = y + row;
        if (screen_y < 0 || screen_y >= HEIGHT) continue;
        int page = screen_y / 8;
        int bit = screen_y % 8;
        uint8_t mask = 1 << bit;
        for (int col = 0; col < 8; col++) {
            if (glyph[row] & (1 << (7 - col))) {
                int px = x + col;
                if (px >= 0 && px < WIDTH)
                    _fb[page * WIDTH + px] |= mask;
            }
        }
    }
    (void)large; // 16x16 not implemented; fallback to 8x8
}

static void draw_str(int x, int y, const char *s, bool large) {
    int cx = x;
    while (*s) {
        draw_char(cx, y, *s, large);
        cx += large ? 16 : 8;
        s++;
    }
}

static int str_width(const char *s, bool large) {
    return strlen(s) * (large ? 16 : 8);
}

static void fill_rect(int x, int y, int w, int h) {
    for (int row = y; row < y + h && row < HEIGHT; row++) {
        int page = row / 8;
        int bit = row % 8;
        uint8_t mask = 1 << bit;
        for (int col = x; col < x + w && col < WIDTH; col++)
            _fb[page * WIDTH + col] |= mask;
    }
}

// ---- Public API ----

bool OLEDDisplay::init() {
    gpio_set_direction((gpio_num_t)OLED_SCL, GPIO_MODE_OUTPUT_OD);
    gpio_set_direction((gpio_num_t)OLED_SDA, GPIO_MODE_OUTPUT_OD);
    scl_high(); sda_high();
    oled_init_seq();
    _ok = true;
    return true;
}

void OLEDDisplay::showCentered(const std::string &text, int y, int size) {
    if (!_ok) return;
    memset(_fb, 0, sizeof(_fb));
    int w = str_width(text.c_str(), size >= 2);
    draw_str((WIDTH - w) / 2, y, text.c_str(), size >= 2);
    oled_flush();
}

void OLEDDisplay::showStatus(const std::string &line1, const std::string &line2) {
    if (!_ok) return;
    memset(_fb, 0, sizeof(_fb));
    draw_str(0, 12, line1.c_str(), false);
    if (!line2.empty()) draw_str(0, 28, line2.c_str(), false);
    oled_flush();
}

void OLEDDisplay::drawVolumeBar(int level) {
    if (!_ok) return;
    memset(_fb, 0, sizeof(_fb));
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    draw_str(0, 12, "MIC Test", false);
    int barX = 10, barY = 24, barW = 108, barH = 16;
    fill_rect(barX, barY, barW, barH);
    int fillW = (level * (barW - 4)) / 100;
    // Clear inside and refill
    memset(_fb, 0, sizeof(_fb));
    draw_str(0, 12, "MIC Test", false);
    // Draw frame as filled border
    fill_rect(barX, barY, barW, 1);
    fill_rect(barX, barY + barH - 1, barW, 1);
    fill_rect(barX, barY + 1, 1, barH - 2);
    fill_rect(barX + barW - 1, barY + 1, 1, barH - 2);
    // Fill bar
    if (fillW > 0) fill_rect(barX + 2, barY + 2, fillW, barH - 4);
    char pct[8]; snprintf(pct, sizeof(pct), "%d%%", level);
    int w = str_width(pct, false);
    draw_str((WIDTH - w) / 2, 56, pct, false);
    oled_flush();
}

void OLEDDisplay::bootAnimation() {
    if (!_ok) return;
    memset(_fb, 0, sizeof(_fb));
    draw_str(8, 36, "Link Start!", false);
    draw_str(104, 60, FIRMWARE_VERSION, false);
    oled_flush();
    vTaskDelay(pdMS_TO_TICKS(1500));
}

void OLEDDisplay::clear() {
    if (!_ok) return;
    memset(_fb, 0, sizeof(_fb));
    oled_flush();
}
