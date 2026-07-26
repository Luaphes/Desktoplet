#include "websocket.h"
#include <esp_log.h>
#include <esp_websocket_client.h>
#include <cstring>

static const char *TAG = "WS";

WSClient wsClient;

static void ws_event_handler(void *handler_args, esp_event_base_t base,
                             int32_t event_id, void *event_data) {

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected");
        wsClient._connected = true;
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected");
        wsClient._connected = false;
        break;
    case WEBSOCKET_EVENT_DATA: {
        auto *data = (esp_websocket_event_data_t *)event_data;
        if (data->op_code == 1 && data->data_len > 0) {
            std::string msg((char *)data->data_ptr, data->data_len);
            ESP_LOGI(TAG, "RX: %s", msg.c_str());
            if (wsClient._onMsg) {
                wsClient._onMsg(msg);
            }
        }
        break;
    }
    default:
        break;
    }
}

void WSClient::init(const std::string &host, uint16_t port) {
    _host = host;
    _port = port;

    esp_websocket_client_config_t cfg = {};
    cfg.host = _host.c_str();
    cfg.port = _port;
    cfg.path = "/";
    cfg.keep_alive_enable = true;
    cfg.keep_alive_idle = 5000;
    cfg.keep_alive_interval = 3000;
    cfg.network_timeout_ms = 10000;
    cfg.buffer_size = 2048;
    cfg.task_stack = 4096;
    cfg.disable_pingpong_discon = true;
    cfg.transport = WEBSOCKET_TRANSPORT_OVER_TCP;

    _handle = esp_websocket_client_init(&cfg);
    if (!_handle) {
        ESP_LOGE(TAG, "Failed to init WebSocket");
        return;
    }

    esp_websocket_register_events(
        (esp_websocket_client_handle_t)_handle,
        WEBSOCKET_EVENT_ANY, ws_event_handler, _handle);

    esp_websocket_client_start((esp_websocket_client_handle_t)_handle);
}

void WSClient::loop() {
    // Reconnection is handled automatically by esp_websocket_client
}

void WSClient::send(const std::string &msg) {
    if (!_handle || !_connected) return;
    esp_websocket_client_send_text(
        (esp_websocket_client_handle_t)_handle,
        msg.c_str(), msg.length(), pdMS_TO_TICKS(1000));
}

bool WSClient::isConnected() { return _connected; }

void WSClient::onMessage(std::function<void(const std::string &)> cb) {
    _onMsg = cb;
}
