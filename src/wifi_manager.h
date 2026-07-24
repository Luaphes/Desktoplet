#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

class WiFiManager {
public:
    void init();
    void startConfigPortal();
    bool isConnected();
    bool needsConfig();
    void clearAndRestart();
    String getECSAddress();
    int getECSPort();
    String getLocalIP();
};

extern WiFiManager wifiManager;

#endif
