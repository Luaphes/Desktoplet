#include "websocket.h"
#include "pins.h"
#include <WebSocketsClient.h>

static WebSocketsClient ws;
static std::function<void(const String &)> _callback = nullptr;
static String _host = "";
static int _port = WS_PORT;
static bool _connected = false;

void WSClient::init(const String &host, int port) {
    _host = host;
    _port = port;
    ws.begin(_host.c_str(), _port, "/");
    ws.onEvent([](WStype_t type, uint8_t *payload, size_t len) {
        switch (type) {
            case WStype_DISCONNECTED:
                _connected = false;
                break;
            case WStype_CONNECTED:
                _connected = true;
                break;
            case WStype_TEXT: {
                if (_callback && payload) {
                    String msg = String((char *)payload);
                    _callback(msg);
                }
                break;
            }
            default:
                break;
        }
    });
    ws.setReconnectInterval(WS_RECONNECT_SEC * 1000);
}

void WSClient::loop() {
    ws.loop();
}

bool WSClient::isConnected() {
    return _connected;
}

void WSClient::send(const String &data) {
    if (_connected) {
        String payload = data;
        ws.sendTXT(payload);
    }
}

void WSClient::sendJson(JsonDocument &doc) {
    String buf;
    serializeJson(doc, buf);
    send(buf);
}

void WSClient::onMessage(std::function<void(const String &)> callback) {
    _callback = callback;
}

void WSClient::disconnect() {
    ws.disconnect();
    _connected = false;
}

WSClient wsClient;
