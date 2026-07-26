#pragma once
#include <string>
#include <functional>

class LWIP_WS {
public:
    bool connect(const char *host, int port);
    void disconnect();
    bool send(const std::string &msg);
    bool isConnected() { return _connected; }
    void onMessage(std::function<void(const std::string&)> cb);
    void loop() {}  // 兼容旧接口
    int _sock = -1;
    bool _connected = false;
    std::function<void(const std::string&)> _onMsg;
};

extern LWIP_WS ws;
