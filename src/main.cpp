#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebSocketsClient.h>
#include <HTTPUpdate.h>
#include <U8g2lib.h>
#include <driver/i2s.h>
#include <Preferences.h>

// ---- OLED (SSD1315 via U8g2 HW I2C) ----
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

// ---- WebSocket ----
WebSocketsClient ws;
Preferences prefs;

const char *ECS_HOST = "118.31.46.156";
const uint16_t ECS_PORT = 8765;
const char *OTA_PATH = "/firmware.bin";

// ---- I2S (INMP441, old API) ----
#define I2S_WS   2
#define I2S_SCK  3
#define I2S_SD   4

void initI2S() {
    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = 64,
        .use_apll = false,
    };
    i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
    i2s_pin_config_t pin = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD,
    };
    i2s_set_pin(I2S_NUM_0, &pin);
    i2s_start(I2S_NUM_0);
}

// ---- WS Event ----
void wsEvent(WStype_t type, uint8_t *data, size_t len) {
    if (type == WStype_TEXT) {
        String msg((char*)data, len);
        if (msg.indexOf("\"ota\"") >= 0) {
            // Parse URL and OTA
            int u = msg.indexOf("\"url\":\"");
            if (u >= 0) {
                u += 7;
                int e = msg.indexOf("\"", u);
                String url = msg.substring(u, e);
            // OTA via WiFiClient
            WiFiClient client;
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr);
            u8g2.drawStr(0, 30, "OTA Update...");
            u8g2.sendBuffer();
            t_httpUpdate_return ret = httpUpdate.update(client, url);
                if (ret == HTTP_UPDATE_OK) {
                    ESP.restart();
                }
            }
        } else if (msg.indexOf("\"display\"") >= 0) {
            int t = msg.indexOf("\"text\":\"");
            String text = "";
            if (t >= 0) {
                t += 8;
                int e = msg.indexOf("\"", t);
                text = msg.substring(t, e);
            }
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr);
            int tw = u8g2.getStrWidth(text.c_str());
            u8g2.drawStr((128 - tw) / 2, 32, text.c_str());
            u8g2.sendBuffer();
        }
        ws.sendTXT("{\"type\":\"ack\"}");
    }
}

void setup() {
    Serial.begin(115200);
    prefs.begin("desktoppy", false);

    // OLED
    u8g2.begin();
    u8g2.setFlipMode(0);
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB14_tr);
    u8g2.drawStr(8, 36, "Link Start!");
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(104, 60, FIRMWARE_VERSION);
    u8g2.sendBuffer();
    delay(1500);

    // WiFi
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 20, "WiFi Config");
    u8g2.drawStr(0, 36, "Connect to:");
    u8g2.drawStr(0, 52, "ESP32-Config");
    u8g2.sendBuffer();
    
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    if (!wm.autoConnect("ESP32-Config")) {
        // timeout, restart and retry
        ESP.restart();
    }

    // Connected — save to prefs for next boot
    prefs.putString("ssid", WiFi.SSID());
    prefs.putString("pass", WiFi.psk());

    // Display status
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 12, "WiFi OK");
    String ip = WiFi.localIP().toString();
    u8g2.drawStr(0, 28, ip.c_str());
    u8g2.sendBuffer();

    // I2S init
    initI2S();

    // WebSocket
    ws.begin(ECS_HOST, ECS_PORT, "/");
    ws.onEvent(wsEvent);
    ws.setReconnectInterval(5000);
}

void loop() {
    ws.loop();
}
