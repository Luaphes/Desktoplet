#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebSocketsClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <U8g2lib.h>
#include <driver/i2s.h>
#include <esp_task_wdt.h>
#include "hermes_logo.h"

// ---- OLED (SSD1315 via U8g2 HW I2C) ----
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

// ---- WebSocket ----
WebSocketsClient ws;
bool micTestActive = false;
unsigned long micTestEnd = 0;

// ---- Deferred OTA (processed in loop, not WS callback) ----
bool otaPending = false;
String otaUrl = "";

// ---- OLED burn-in prevention: corner rotation ----
static int g_verCorner = 3;              // 0=TL 1=TR 2=BL 3=BR
static unsigned long g_verNextJump = 0;

const char *ECS_HOST = "118.31.46.156";
const uint16_t ECS_PORT = 8765;
const char *OTA_PATH = "/firmware.bin";

// ---- I2S (INMP441, old API) ----
#define I2S_WS   2
#define I2S_SCK  3
#define I2S_SD   4
#define BTN_PIN  0

void initI2S() {
    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 6,
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

// ---- Version display (burn-in safe: 30min corner rotation) ----
void drawVersionCorner() {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    int tw = u8g2.getStrWidth(FIRMWARE_VERSION);
    int x, y;
    switch (g_verCorner) {
        case 0: x = 2;               y = 8;  break;  // TL
        case 1: x = 128 - tw - 2;    y = 8;  break;  // TR
        case 2: x = 2;               y = 64; break;  // BL
        case 3: x = 128 - tw - 2;    y = 64; break;  // BR
    }
    u8g2.drawStr(x, y, FIRMWARE_VERSION);
}

// ---- OTA progress callback ----
void otaProgress(int cur, int total) {
    if (total <= 0) return;
    int pct = (cur * 100) / total;
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 12, "OTA Update");
    u8g2.drawFrame(10, 24, 108, 16);
    int fw = pct * 104 / 100;
    if (fw > 0) u8g2.drawBox(12, 26, fw, 12);
    char buf[16];
    sprintf(buf, "%d%%", pct);
    u8g2.setCursor((128 - u8g2.getStrWidth(buf)) / 2, 56);
    u8g2.print(buf);
    drawVersionCorner();
    u8g2.sendBuffer();
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
                otaPending = true;
                otaUrl = url;
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
            drawVersionCorner();
            u8g2.sendBuffer();
        } else if (msg.indexOf("\"mic_test\"") >= 0) {
            // MIC test: set flag, main loop handles to avoid blocking WS
            int dur = 5;
            int d = msg.indexOf("\"duration\":");
            if (d >= 0) dur = msg.substring(d + 11).toInt();
            micTestEnd = millis() + dur * 1000;
            micTestActive = true;
        }
        ws.sendTXT("{\"type\":\"ack\"}");
    }
}

void setup() {
    // DIAG: no Serial on C3 — use OLED heartbeat instead

    // Boot animation — Hermes logo (64x64) + Desktoppy label
    u8g2.begin();
    u8g2.setFlipMode(0);
    u8g2.clearBuffer();
    u8g2.drawXBM(0, 0, 64, 64, hermes_logo);
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(70, 30, "Desktoppy");
    drawVersionCorner();
    u8g2.sendBuffer();
    delay(1500);

    // ---- Hold BTN 3s to reset WiFi ----
    pinMode(BTN_PIN, INPUT_PULLUP);
    {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(0, 20, "Hold BTN 3s");
        u8g2.drawStr(0, 36, "to reset WiFi");
        drawVersionCorner();
        u8g2.sendBuffer();

        unsigned long t0 = millis();
        bool resetWiFi = false;
        while (millis() - t0 < 5000) {
            if (digitalRead(BTN_PIN) == LOW) {
                unsigned long held = millis();
                while (digitalRead(BTN_PIN) == LOW) {
                    if (millis() - held > 3000) { resetWiFi = true; break; }
                    delay(10);
                }
                if (resetWiFi) break;
            }
            delay(10);
        }
        if (resetWiFi) {
            WiFiManager wmReset;
            wmReset.resetSettings();
            u8g2.clearBuffer();
            u8g2.drawStr(0, 28, "WiFi Reset OK");
            drawVersionCorner();
            u8g2.sendBuffer();
            delay(2000);
            ESP.restart();
        }
    }

    // WiFi
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 20, "Connecting...");
    drawVersionCorner();
    u8g2.sendBuffer();
    
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    wm.setWiFiAutoReconnect(true);
    if (!wm.autoConnect("ESP32-Config")) {
        ESP.restart();
    }

    // WiFiManager saves credentials internally, no need for manual prefs

    // Display status
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    int tw = u8g2.getStrWidth("WiFi OK");
    u8g2.drawStr((128 - tw) / 2, 14, "WiFi OK");
    String ip = WiFi.localIP().toString();
    int iw = u8g2.getStrWidth(ip.c_str());
    u8g2.drawStr((128 - iw) / 2, 34, ip.c_str());
    drawVersionCorner();
    u8g2.sendBuffer();

    // I2S init — RE-ENABLED for Build B
    initI2S();
    pinMode(BTN_PIN, INPUT_PULLUP);

    // WebSocket
    ws.begin(ECS_HOST, ECS_PORT, "/");
    ws.onEvent(wsEvent);
    ws.setReconnectInterval(5000);

    // OTA rollback: confirm this firmware is stable
    esp_ota_mark_app_valid_cancel_rollback();
}

void loop() {
    esp_task_wdt_reset();  // DIAG: feed task watchdog
    ws.loop();
    
    // ---- Deferred OTA (non-blocking flag, actual download here) ----
    if (otaPending) {
        WiFiClientSecure client;
        client.setInsecure();  // skip CA check for trusted GitHub CDN
        HTTPUpdate updater;
        updater.onProgress(otaProgress);
        t_httpUpdate_return ret = updater.update(client, otaUrl);
        if (ret == HTTP_UPDATE_OK) {
            ESP.restart();
        } else {
            const char *err = (ret == HTTP_UPDATE_FAILED) ? "OTA Failed" : "No Update";
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr);
            u8g2.drawStr(0, 28, err);
            drawVersionCorner();
            u8g2.sendBuffer();
            delay(3000);
        }
        otaPending = false;
        otaUrl = "";
    }
    
    // 30min corner rotation (OLED burn-in prevention)
    if (millis() - g_verNextJump > 30 * 60 * 1000UL) {
        g_verCorner = (g_verCorner + 1) % 4;
        g_verNextJump = millis();
    }
    
    // Button-triggered I2S streaming
    static bool btnHeld = false;
    static unsigned long btnDown = 0;
    bool pressed = (digitalRead(BTN_PIN) == LOW);
    
    if (pressed && !btnHeld && btnDown == 0) {
        btnDown = millis();
    } else if (pressed && !btnHeld && millis() - btnDown > 100) {
        btnHeld = true;
        ws.sendTXT("{\"type\":\"mic_start\"}");
    } else if (!pressed) {
        if (btnHeld) {
            ws.sendTXT("{\"type\":\"mic_stop\"}");
            // Restore WiFi OK display
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr);
            int tw3 = u8g2.getStrWidth("WiFi OK");
            u8g2.drawStr((128 - tw3) / 2, 14, "WiFi OK");
            String rip = WiFi.localIP().toString();
            int iw3 = u8g2.getStrWidth(rip.c_str());
            u8g2.drawStr((128 - iw3) / 2, 34, rip.c_str());
            drawVersionCorner();
            u8g2.sendBuffer();
        }
        btnHeld = false;
        btnDown = 0;
    }
    
    if (btnHeld) {
        int16_t buf[128];
        size_t bytes = 0;
        i2s_read(I2S_NUM_0, buf, sizeof(buf), &bytes, 0);
        if (bytes > 0) {
            ws.sendBIN((uint8_t*)buf, bytes);
        }
        // Volume bar on OLED
        int samples = bytes / 2;
        int32_t sum = 0;
        for (int i = 0; i < samples; i++) {
            int32_t v = buf[i]; if (v < 0) v = -v;
            sum += v;
        }
        int vol = samples ? (sum / samples) * 100 / 256 : 0;
        if (vol > 100) vol = 100;
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(0, 12, "REC");
        u8g2.drawFrame(10, 24, 108, 16);
        int fw = vol * 104 / 100;
        if (fw > 0) u8g2.drawBox(12, 26, fw, 12);
        char p[8]; sprintf(p, "%d%%", vol);
        u8g2.setCursor((128 - u8g2.getStrWidth(p)) / 2, 56);
        u8g2.print(p);
        drawVersionCorner();
        u8g2.sendBuffer();
    }
    
    // mic_test via WS command (non-blocking)
    if (micTestActive) {
        if (millis() >= micTestEnd) {
            micTestActive = false;
            ws.sendTXT("{\"type\":\"mic_done\"}");
            // Restore WiFi OK display
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr);
            int tw2 = u8g2.getStrWidth("WiFi OK");
            u8g2.drawStr((128 - tw2) / 2, 14, "WiFi OK");
            String rip = WiFi.localIP().toString();
            int iw2 = u8g2.getStrWidth(rip.c_str());
            u8g2.drawStr((128 - iw2) / 2, 34, rip.c_str());
            drawVersionCorner();
            u8g2.sendBuffer();
        } else {
            int16_t buf[64];
            size_t bytes = 0;
            i2s_read(I2S_NUM_0, buf, sizeof(buf), &bytes, 0);
            int samples = bytes / 2;
            int32_t sum = 0;
            for (int i = 0; i < samples; i++) {
                int32_t v = buf[i]; if (v < 0) v = -v;
                sum += v;
            }
            int vol = samples ? (sum / samples) * 100 / 256 : 0;
            if (vol > 100) vol = 100;
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr);
            u8g2.drawStr(0, 12, "MIC Test");
            u8g2.drawFrame(10, 24, 108, 16);
            int fw = vol * 104 / 100;
            if (fw > 0) u8g2.drawBox(12, 26, fw, 12);
            char p[8]; sprintf(p, "%d%%", vol);
            u8g2.setCursor((128 - u8g2.getStrWidth(p)) / 2, 56);
            u8g2.print(p);
            drawVersionCorner();
            u8g2.sendBuffer();
        }
    }

    // DIAG: OLED heartbeat — blink version number every 1s
    static unsigned long lastBeat = 0;
    static bool showVer = true;
    if (millis() - lastBeat > 1000) {
        showVer = !showVer;
        // Redraw idle screen: WiFi OK + IP + (maybe) version
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        int tw = u8g2.getStrWidth("WiFi OK");
        u8g2.drawStr((128 - tw) / 2, 14, "WiFi OK");
        String ip = WiFi.localIP().toString();
        int iw = u8g2.getStrWidth(ip.c_str());
        u8g2.drawStr((128 - iw) / 2, 34, ip.c_str());
        if (showVer) drawVersionCorner();
        u8g2.sendBuffer();
        lastBeat = millis();
    }
}
