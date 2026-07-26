#include <stdio.h>
#include <string>
#include <cstring>
#include <esp_timer.h>
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
#include "lwip_ws.h"

static const char *TAG = "Main";

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

// 手写 JSON 字段提取，不依赖任何 JSON 库
static std::string get_json_str(const std::string &json, const std::string &key) {
    std::string search = "\"" + key + "\":\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.length();
    size_t end = json.find("\"", pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

static int get_json_int(const std::string &json, const std::string &key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.length();
    char *end;
    return strtol(json.c_str() + pos, &end, 10);
}

static void handleWSMessage(const std::string &msg) {
    // 回显确认收到消息
    ws.send("{\"type\":\"ack\"}");
    
    std::string type = get_json_str(msg, "type");
    if (type.empty()) return;

    if (type == "ota") {
        std::string url = get_json_str(msg, "url");
        if (!url.empty()) {
            _state = STATE_OTA;
            ESP_LOGI(TAG, "OTA from: %s", url.c_str());
            otaManager.startOTA(url);
        }
    } else if (type == "display") {
        ESP_LOGI(TAG, "display: %s", get_json_str(msg, "text").c_str());
    } else if (type == "chinese") {
        ESP_LOGI(TAG, "chinese: %s", get_json_str(msg, "text").c_str());
    } else if (type == "mic_test") {
        int seconds = get_json_int(msg, "duration");
        if (seconds <= 0) seconds = 5;
        mic.start();
        _micTestEnd = (esp_timer_get_time() / 1000) + (seconds * 1000);
        _state = STATE_MIC_TEST;
        display.showCentered("MIC Test", 20);
        ESP_LOGI(TAG, "MIC test %ds", seconds);
    }
}

void onWiFiConnected() {
    _state = STATE_RUNNING;
    ESP_LOGI(TAG, "WiFi OK");
    display.showStatus("WiFi OK", wifiManager.getLocalIP().c_str());
    ws.connect(wifiManager.getECSAddress().c_str(), wifiManager.getECSPort());
    ws.onMessage(handleWSMessage);
}

extern "C" void app_main() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    button.init();
    display.init();
    display.bootAnimation();
    mic.init();
    otaManager.init();
    wifiManager.init();

    if (wifiManager.needsConfig()) {
        _state = STATE_CONFIG;
        display.showCentered("Config Mode", 20);
        wifiManager.startConfigPortal();
    } else {
        _state = STATE_CONNECTING;
        display.showCentered("Connecting...", 20);
        ESP_LOGI(TAG, "Connecting...");
    }

    while (true) {
        if (_state == STATE_MIC_TEST) {
            unsigned long now = esp_timer_get_time() / 1000;
            if (now >= _micTestEnd) {
                _micTestEnd = 0;
                _state = STATE_RUNNING;
                ESP_LOGI(TAG, "MIC test done");
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
                    ESP_LOGI(TAG, "mic level: %d", avg);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        wsClient.loop();

        int btn = button.check();
        if (btn == 1 && wsClient.isConnected()) {
            wsClient.send("{\"type\":\"btn_click\"}");
        } else if (btn == 2) {
            vTaskDelay(pdMS_TO_TICKS(500));
            wifiManager.clearAndRestart();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
