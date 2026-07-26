#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <string>
#include <functional>

class WSClient {
public:
    void init(const std::string &host, uint16_t port);
    void loop();
    void send(const std::string &msg);
    bool isConnected();
    void onMessage(std::function<void(const std::string &)> cb);
private:
    void *_handle = nullptr;
    std::function<void(const std::string &)> _onMsg;
    std::string _host;
    uint16_t _port = 8765;
public:
    bool _connected = false;
};

extern WSClient wsClient;

#endif
