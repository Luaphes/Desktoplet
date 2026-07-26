#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <string>
#include <esp_event.h>

class WiFiManager {
public:
    void init();
    void startConfigPortal();
    bool isConnected();
    bool needsConfig();
    void clearAndRestart();
    std::string getLocalIP();
    std::string getECSAddress();
    uint16_t getECSPort();
};

extern WiFiManager wifiManager;

#endif
