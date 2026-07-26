#include <stdio.h>
#include <string>
#include <cJSON.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "pins.h"
#include "version.h"
#include "oled_display.h"
#include "button.h"
#include "mic_i2s.h"
#include "ota.h"
#include "wifi_manager.h"
#include "websocket.h"

static const char *TAG = "Main";

// ---- 系统状态 ----
enum State {
    STATE_BOOT,
    STATE_CONFIG,
    STATE_CONNECTING,
    STATE_RUNNING,
    STATE_MIC_TEST,
    STATE_OTA,
};
static State _state = STATE_BOOT;
static unsigned long _micTestEnd = 0;
static int16_t _micBuf[128];

// ---- WS 消息处理 ----
static void handleWSMessage(const std::string &msg) {
    cJSON *doc = cJSON_Parse(msg.c_str());
    if (!doc) return;

    cJSON *type = cJSON_GetObjectItem(doc, "type");
    if (!type || !cJSON_IsString(type)) { cJSON_Delete(doc); return; }

    std::string t = type->valuestring;

    if (t == "ota") {
        cJSON *url = cJSON_GetObjectItem(doc, "url");
        if (url && cJSON_IsString(url)) {
            _state = STATE_OTA;
            display.showStatus("OTA...", "");
            otaManager.startOTA(url->valuestring);
        }
    } else if (t == "display") {
        cJSON *text = cJSON_GetObjectItem(doc, "text");
        if (text && cJSON_IsString(text)) {
            display.showCentered(text->valuestring, 20, 1);
        }
    } else if (t == "status") {
        cJSON *l1 = cJSON_GetObjectItem(doc, "line1");
        cJSON *l2 = cJSON_GetObjectItem(doc, "line2");
        display.showStatus(
            l1 ? l1->valuestring : "",
            l2 ? l2->valuestring : "");
    } else if (t == "clear") {
        display.clear();
    } else if (t == "chinese") {
        cJSON *text = cJSON_GetObjectItem(doc, "text");
        cJSON *x = cJSON_GetObjectItem(doc, "x");
        cJSON *y = cJSON_GetObjectItem(doc, "y");
        if (text && cJSON_IsString(text)) {
            if (x && cJSON_IsNumber(x))
                display.showChinese(text->valuestring, x->valueint, y ? y->valueint : 20);
            else
                display.chineseCentered(text->valuestring, y ? y->valueint : 20);
        }
    } else if (t == "bitmap") {
        cJSON *x = cJSON_GetObjectItem(doc, "x");
        cJSON *y = cJSON_GetObjectItem(doc, "y");
        cJSON *w = cJSON_GetObjectItem(doc, "w");
        cJSON *h = cJSON_GetObjectItem(doc, "h");
        cJSON *data = cJSON_GetObjectItem(doc, "data");
        if (cJSON_IsArray(data) && w && h && w->valueint > 0 && h->valueint > 0) {
            int len = cJSON_GetArraySize(data);
            uint8_t *buf = new uint8_t[len];
            for (int i = 0; i < len; i++) {
                cJSON *item = cJSON_GetArrayItem(data, i);
                buf[i] = item ? item->valueint : 0;
            }
            display.drawBitmap(x ? x->valueint : 0, y ? y->valueint : 0,
                               w->valueint, h->valueint, buf);
            delete[] buf;
        }
    } else if (t == "mic_test") {
        cJSON *dur = cJSON_GetObjectItem(doc, "duration");
        int seconds = dur ? dur->valueint : 5;
        mic.start();
        _micTestEnd = (esp_timer_get_time() / 1000) + (seconds * 1000);
        _state = STATE_MIC_TEST;
        display.chineseCentered("MIC 测试", 20);
        display.chineseCentered("吹口气看看", 48);
    }

    cJSON_Delete(doc);
}

// ---- WiFi 连接成功回调 ----
void onWiFiConnected() {
    ESP_LOGI(TAG, "WiFi connected!");
    _state = STATE_RUNNING;
    display.showStatus("WiFi OK", wifiManager.getLocalIP().c_str());
    wsClient.init(wifiManager.getECSAddress(), wifiManager.getECSPort());
    wsClient.onMessage(handleWSMessage);
}

// ---- app_main ----
extern "C" void app_main() {
    // Init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    display.init();
    display.bootAnimation();
    button.init();
    mic.init();
    otaManager.init();
    wifiManager.init();

    if (wifiManager.needsConfig()) {
        _state = STATE_CONFIG;
        display.showCentered("Config Mode", 20);
        wifiManager.startConfigPortal();
    } else {
        _state = STATE_CONNECTING;
        display.showStatus("Connecting...", "");
    }

    // Main loop
    while (true) {
        if (_state == STATE_MIC_TEST) {
            unsigned long now = esp_timer_get_time() / 1000;
            if (now >= _micTestEnd) {
                _micTestEnd = 0;
                _state = STATE_RUNNING;
                display.showStatus("WiFi OK", wifiManager.getLocalIP().c_str());
            } else {
                int n = mic.readData(_micBuf, 16);
                if (n > 0) {
                    int32_t sum = 0;
                    for (int i = 0; i < n; i++) {
                        int32_t v = _micBuf[i];
                        if (v < 0) v = -v;
                        sum += v;
                    }
                    int avg = sum / n;
                    int level = (avg > 5000) ? 100 : (avg * 100) / 5000;
                    if (level > 100) level = 100;
                    display.drawVolumeBar(level);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        // Normal loop
        wsClient.loop();

        // Button check
        int btn = button.check();
        if (btn == 1 && wsClient.isConnected()) {
            wsClient.send("{\"type\":\"btn_click\"}");
        } else if (btn == 2) {
            display.showCentered("Reset WiFi...", 20, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            wifiManager.clearAndRestart();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
