#ifndef OTA_H
#define OTA_H

#include <string>

class OTAManager {
public:
    void init();
    void startOTA(const std::string &url);
    bool isUpdating();
};

extern OTAManager otaManager;

#endif
