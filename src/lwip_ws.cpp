/*
 * lwip_ws.cpp — 基于 LWIP socket 的 WebSocket 客户端
 * 不依赖 esp_websocket_client 组件，直接 TCP + WebSocket 握手
 */
#include "lwip_ws.h"
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <cstdlib>
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>

static const char *TAG = "LWIP_WS";

LWIP_WS ws;

// 生成 WebSocket 握手用的 Sec-WebSocket-Key
static std::string ws_gen_key() {
    uint8_t buf[16];
    for (int i = 0; i < 16; i++) buf[i] = rand() & 0xff;
    size_t olen = 0;
    unsigned char out[32];
    mbedtls_base64_encode(out, sizeof(out), &olen, buf, 16);
    return std::string((char*)out, olen);
}

bool LWIP_WS::connect(const char *host, int port) {
    _connected = false;
    _sock = -1;

    struct hostent *he = gethostbyname(host);
    if (!he) { ESP_LOGE(TAG, "DNS failed: %s", host); return false; }

    _sock = socket(AF_INET, SOCK_STREAM, 0);
    if (_sock < 0) { ESP_LOGE(TAG, "socket failed"); return false; }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);

    struct timeval tv = {10, 0};
    setsockopt(_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (::connect(_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "connect failed");
        close(_sock); _sock = -1; return false;
    }

    // WebSocket 握手
    std::string key = ws_gen_key();
    char req[512];
    snprintf(req, sizeof(req),
        "GET / HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n",
        host, port, key.c_str());

    int n = send(_sock, req, strlen(req), 0);
    if (n <= 0) { close(_sock); _sock = -1; return false; }

    // 读 101 响应
    char rsp[512];
    n = recv(_sock, rsp, sizeof(rsp) - 1, 0);
    if (n <= 0 || !strstr(rsp, "101")) {
        ESP_LOGE(TAG, "handshake failed: %s", rsp);
        close(_sock); _sock = -1; return false;
    }

    _connected = true;
    ESP_LOGI(TAG, "WS connected to %s:%d", host, port);
    return true;
}

void LWIP_WS::disconnect() {
    if (_sock >= 0) { close(_sock); _sock = -1; }
    _connected = false;
}

bool LWIP_WS::send(const std::string &msg) {
    if (_sock < 0 || !_connected) return false;
    uint8_t frame[2 + 128] = {0x81};  // FIN + TEXT opcode
    size_t len = msg.size();
    if (len < 126) {
        frame[1] = len | 0x80;  // mask bit
        memcpy(frame + 2, msg.data(), len);
        // 简单 XOR mask (全零)
        for (size_t i = 0; i < len; i++) frame[2 + i] ^= 0;
        int n = ::send(_sock, frame, 2 + len, 0);
        return n == (int)(2 + len);
    }
    return false;
}

static void ws_recv_task(void *arg) {
    auto *self = (LWIP_WS*)arg;
    uint8_t buf[512];
    while (self->isConnected()) {
        int n = recv(self->_sock, buf, sizeof(buf), 0);
        if (n <= 0) {
            ESP_LOGW(TAG, "recv failed, disconnected");
            self->disconnect();
            break;
        }
        // 解析 WebSocket frame
        if (n >= 2 && (buf[0] & 0x0f) == 1) {  // TEXT opcode
            uint8_t masked = buf[1] & 0x80;
            int len = buf[1] & 0x7f;
            int offset = 2;
            if (len == 126) { len = (buf[2] << 8) | buf[3]; offset = 4; }
            uint8_t mask[4] = {};
            if (masked) {
                memcpy(mask, buf + offset, 4);
                offset += 4;
            }
            char *data = (char*)buf + offset;
            int data_len = n - offset;
            if (data_len > len) data_len = len;
            for (int i = 0; i < data_len; i++) data[i] ^= mask[i % 4];
            std::string msg(data, data_len);
            ESP_LOGI(TAG, "RX: %s", msg.c_str());
            if (self->_onMsg) self->_onMsg(msg);
        }
    }
    vTaskDelete(NULL);
}

void LWIP_WS::onMessage(std::function<void(const std::string&)> cb) {
    _onMsg = cb;
    xTaskCreate(ws_recv_task, "ws_recv", 4096, this, 5, NULL);
}
