#include <stdio.h>
#include <string>
#include <cstdlib>
#include <cJSON.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "pins.h"
#include "version.h"
#include "button.h"
#include "mic_i2s.h"
#include "ota.h"
#include "wifi_manager.h"
#include "websocket.h"

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
            ESP_LOGI(TAG, "OTA from: %s", url->valuestring);
            otaManager.startOTA(url->valuestring);
        }
    } else if (t == "display") {
        cJSON *text = cJSON_GetObjectItem(doc, "text");
        if (text && cJSON_IsString(text))
            ESP_LOGI(TAG, "display: %s", text->valuestring);
    } else if (t == "chinese") {
        cJSON *text = cJSON_GetObjectItem(doc, "text");
        if (text && cJSON_IsString(text))
            ESP_LOGI(TAG, "chinese: %s", text->valuestring);
    } else if (t == "mic_test") {
        cJSON *dur = cJSON_GetObjectItem(doc, "duration");
        int seconds = dur ? dur->valueint : 5;
        mic.start();
        _micTestEnd = (esp_timer_get_time() / 1000) + (seconds * 1000);
        _state = STATE_MIC_TEST;
        ESP_LOGI(TAG, "MIC test %ds", seconds);
    }

    cJSON_Delete(doc);
}

void onWiFiConnected() {
    _state = STATE_RUNNING;
    ESP_LOGI(TAG, "WiFi OK");
    wsClient.init(wifiManager.getECSAddress(), wifiManager.getECSPort());
    wsClient.onMessage(handleWSMessage);
}

extern "C" void app_main() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    button.init();
    mic.init();
    otaManager.init();
    wifiManager.init();

    if (wifiManager.needsConfig()) {
        _state = STATE_CONFIG;
        wifiManager.startConfigPortal();
    } else {
        _state = STATE_CONNECTING;
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
