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
                }
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
