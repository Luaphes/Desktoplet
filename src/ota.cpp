#include "ota.h"
#include <esp_https_ota.h>
#include <esp_log.h>
#include <string.h>

static const char *TAG = "OTA";

void OTAManager::init() {}

void OTAManager::startOTA(const char *url) {
    ESP_LOGI(TAG, "Starting OTA from: %s", url);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA success, restarting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    }
}

bool OTAManager::isUpdating() {
    return false; // synchronous OTA, blocks until done
}

OTAManager otaManager;
