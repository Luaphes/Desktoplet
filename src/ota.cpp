#include "ota.h"
#include <HTTPClient.h>

OTAManager otaManager;

void OTAManager::init() {
    _updating = false;
}

bool OTAManager::isUpdating() {
    return _updating;
}

void OTAManager::startOTA(const String &url) {
    _updating = true;

    HTTPClient http;
    http.begin(url);
    int code = http.GET();

    if (code != 200) {
        http.end();
        _updating = false;
        return;
    }

    int totalSize = http.getSize();
    if (totalSize <= 0) {
        http.end();
        _updating = false;
        return;
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL) {
        http.end();
        _updating = false;
        return;
    }

    esp_ota_handle_t otaHandle;
    esp_err_t err = esp_ota_begin(partition, totalSize, &otaHandle);
    if (err != ESP_OK) {
        http.end();
        _updating = false;
        return;
    }

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buf[256];
    int received = 0;

    while (http.connected() && received < totalSize) {
        int bytes = stream->readBytes(buf, sizeof(buf));
        if (bytes > 0) {
            esp_ota_write(otaHandle, buf, bytes);
            received += bytes;
        }
        // 喂狗，防止重启
        yield();
    }

    http.end();

    if (received == totalSize) {
        esp_ota_end(otaHandle);
        esp_ota_set_boot_partition(partition);
        delay(100);
        ESP.restart();
    } else {
        esp_ota_end(otaHandle);
        _updating = false;
    }
}
