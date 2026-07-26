#ifndef OTA_H
#define OTA_H

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

class OTAManager {
public:
    void init();
    // 从 URL 下载 .bin 并写入 flash
    // 调用后阻塞直到完成或失败，成功则自动重启
    void startOTA(const String &url);
    bool isUpdating();
private:
    bool _updating = false;
};

extern OTAManager otaManager;

#endif
