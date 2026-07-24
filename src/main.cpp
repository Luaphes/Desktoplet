#include <Arduino.h>
#include "pins.h"
#include "wifi_manager.h"
#include "websocket.h"
#include "oled_display.h"
#include "button.h"
#include "mic_i2s.h"
#include "ota.h"

// 当前系统状态
enum SystemState {
    STATE_BOOT,
    STATE_CONNECTING,
    STATE_CONFIG_PORTAL,
    STATE_RUNNING,
    STATE_OTA
};

static SystemState state = STATE_BOOT;
static unsigned long lastWiFiCheck = 0;
static unsigned long _connectStart = 0;
static bool otaInProgress = false;

// ---------- 处理收到的 WebSocket 消息 ----------
void handleWSMessage(const String &msg) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, msg);
    if (err) return;

    String type = doc["type"] | "";

    // OTA 消息
    if (type == "ota") {
        String url = doc["url"] | "";
        if (url.length() > 0) {
            state = STATE_OTA;
            display.showStatus("OTA...", "");
            otaManager.startOTA(url);
        }
        return;
    }

    // 显示消息
    if (type == "display") {
        String text = doc["text"] | "";
        if (text.length() > 0) {
            display.showCentered(text, 20, 1);
        }
        return;
    }

    // 状态消息
    if (type == "status") {
        String line1 = doc["line1"] | "";
        String line2 = doc["line2"] | "";
        display.showStatus(line1, line2);
        return;
    }

    // 双行居中消息
    if (type == "status_center") {
        String line1 = doc["line1"] | "";
        String line2 = doc["line2"] | "";
        display.showCentered(line1, 20, 1);
        display.showCentered(line2, 40, 1);
        return;
    }

    // 清屏
    if (type == "clear") {
        display.clear();
        return;
    }

    // 中文显示
    if (type == "chinese") {
        String text = doc["text"] | "";
        if (text.length() > 0) {
            display.chineseCentered(text, 20);
        }
        return;
    }

    // 点阵图
    if (type == "bitmap") {
        int x = doc["x"] | 0;
        int y = doc["y"] | 0;
        int w = doc["w"] | 0;
        int h = doc["h"] | 0;
        JsonArray arr = doc["data"].as<JsonArray>();
        if (w > 0 && h > 0 && arr.size() > 0) {
            uint8_t *buf = new uint8_t[arr.size()];
            for (size_t i = 0; i < arr.size(); i++) {
                buf[i] = arr[i].as<uint8_t>();
            }
            display.drawBitmap(x, y, w, h, buf);
            delete[] buf;
        }
        return;
    }

    // 多行排版
    if (type == "multi") {
        JsonArray lines = doc["lines"].as<JsonArray>();
        if (lines.size() > 0) {
            OLEDDisplay::Line lineBuf[8];
            uint8_t count = min((uint8_t)lines.size(), (uint8_t)8);
            for (uint8_t i = 0; i < count; i++) {
                JsonObject l = lines[i];
                lineBuf[i].text = l["text"] | "";
                String a = l["align"] | "c";
                lineBuf[i].align = (a == "l") ? 0 : (a == "r") ? 2 : 1;
                lineBuf[i].size = l["size"] | 1;
            }
            display.showMulti(lineBuf, count);
        }
        return;
    }
}

// ---------- setup ----------
void setup() {
    Serial.begin(115200);
    delay(500);

    // OLED 初始化
    if (!display.init()) {
        // OLED 没连上，但系统继续跑（可以通过串口调试）
    }

    // 开机动画
    display.bootAnimation();

    // 按键初始化
    button.init();

    // 麦克风（v0.1 留空）
    mic.init();

    // OTA 初始化
    otaManager.init();

    // WiFi 初始化（读到 NVS 有密码就连，没有就进配网）
    wifiManager.init();

    if (wifiManager.needsConfig()) {
        state = STATE_CONFIG_PORTAL;
        display.showCentered("Config Mode", 20);
    } else {
        state = STATE_CONNECTING;
        display.showStatus("Connecting...", "");
    }
}

// ---------- loop ----------
void loop() {
    // 配网模式（阻塞在 wifi_manager.cpp 里）
    if (state == STATE_CONFIG_PORTAL) {
        // 正常情况下不会跑到这里，wifiManager.startConfigPortal() 是阻塞的
        // 如果它返回了（超时），重启
        delay(1000);
        ESP.restart();
    }

    // OTA 升级中
    if (state == STATE_OTA) {
        display.showStatus("OTA update...", "");
        delay(500);
        if (!otaManager.isUpdating()) {
            state = STATE_RUNNING;
        }
        return;
    }

    // WiFi 状态检查（非阻塞）
    if (state == STATE_CONNECTING || state == STATE_RUNNING) {
        unsigned long now = millis();
        if (now - lastWiFiCheck > 1000) {
            lastWiFiCheck = now;

            if (wifiManager.isConnected()) {
                if (state == STATE_CONNECTING) {
                    state = STATE_RUNNING;
                    display.showStatus("WiFi OK", wifiManager.getLocalIP());
                    // 初始化 WebSocket
                    wsClient.init(wifiManager.getECSAddress(), wifiManager.getECSPort());
                    wsClient.onMessage(handleWSMessage);
                }
            } else {
                if (state == STATE_RUNNING) {
                    // 断线了
                    state = STATE_CONNECTING;
                    display.showStatus("WiFi lost", "Reconnecting...");
                    _connectStart = now;
                }
            }
        }

        // WiFi 重连超时（2 分钟不进配网 → 清 NVS 进配网）
        if (state == STATE_CONNECTING && !wifiManager.isConnected()) {
            if (now - _connectStart > 120000) {
                wifiManager.clearAndRestart();
            }
        }
    }

    // WebSocket loop
    wsClient.loop();

    // 按键检测
    int btn = button.check();
    if (btn == 1) {
        // 短按 → 通知 ECS
        if (wsClient.isConnected()) {
            wsClient.send("{\"type\":\"btn_click\"}");
        }
    } else if (btn == 2) {
        // 长按 5 秒 → 清 NVS 重配网
        display.showCentered("Reset WiFi...", 20, 1);
        delay(500);
        wifiManager.clearAndRestart();
    }

    delay(10);
}
