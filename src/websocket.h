#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <Arduino.h>
#include <ArduinoJson.h>

class WSClient {
public:
    void init(const String &host, int port);
    void loop();
    bool isConnected();
    void send(const String &data);
    void sendJson(JsonDocument &doc);
    void onMessage(std::function<void(const String &)> callback);
    void disconnect();
};

extern WSClient wsClient;

#endif
