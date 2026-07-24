#include "wifi_manager.h"
#include "pins.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <nvs_flash.h>
#include <nvs.h>

static const char *nvs_namespace = NVS_NAMESPACE;

// ---------- Captive Portal ----------
static WebServer server(80);
static DNSServer dns;
static const byte DNS_PORT = 53;
static const char *AP_SSID = "ESP32-Config";
static const char *AP_PASS = "";

static String cfg_ssid = "";
static String cfg_pass = "";
static String cfg_ecs_addr = "";
static int cfg_ecs_port = WS_PORT;

static const char portal_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>ESP32 配网</title>
<style>
body{font-family:sans-serif;background:#f5f5f5;padding:20px;max-width:400px;margin:auto}
h2{text-align:center}
input{width:100%;padding:10px;margin:8px 0;border:1px solid #ccc;border-radius:6px}
button{width:100%;padding:12px;background:#007aff;color:white;border:none;border-radius:6px;font-size:16px}
</style>
</head>
<body>
<h2>ESP32 配网</h2>
<form action="/save" method="POST">
WiFi 名称:<br><input name="ssid" required>
WiFi 密码:<br><input type="password" name="pass" required>
ECS 地址:<br><input name="ecs" placeholder="192.168.x.x" required>
ECS 端口:<br><input name="port" value="8765">
<button type="submit">保存并重启</button>
</form>
</body>
</html>
)rawliteral";

static void handleRoot() {
    server.send(200, "text/html", portal_html);
}

static void handleSave() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    String ecs  = server.arg("ecs");
    String port = server.arg("port");
    if (ssid.length() == 0 || ecs.length() == 0) {
        server.send(200, "text/html", "<h3>请填写必要字段</h3><a href='/'>返回</a>");
        return;
    }

    // 存 NVS
    nvs_handle_t nvs;
    if (nvs_open(nvs_namespace, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, "wifi_ssid", ssid.c_str());
        nvs_set_str(nvs, "wifi_pass", pass.c_str());
        nvs_set_str(nvs, "ecs_addr", ecs.c_str());
        nvs_set_i32(nvs, "ecs_port", port.toInt());
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    server.send(200, "text/html", "<h3>已保存，重启中...</h3>");
    delay(100);
    ESP.restart();
}

static void handleNotFound() {
    server.send(200, "text/html", portal_html);
}

// ---------- WiFiManager ----------
void WiFiManager::init() {
    nvs_flash_init();

    nvs_handle_t nvs;
    if (nvs_open(nvs_namespace, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = 64;
        char buf[64];
        if (nvs_get_str(nvs, "wifi_ssid", buf, &len) == ESP_OK) {
            cfg_ssid = String(buf);
        }
        len = 64;
        if (nvs_get_str(nvs, "wifi_pass", buf, &len) == ESP_OK) {
            cfg_pass = String(buf);
        }
        len = 64;
        if (nvs_get_str(nvs, "ecs_addr", buf, &len) == ESP_OK) {
            cfg_ecs_addr = String(buf);
        }
        int32_t port = WS_PORT;
        nvs_get_i32(nvs, "ecs_port", &port);
        cfg_ecs_port = port;
        nvs_close(nvs);
    }

    if (cfg_ssid.length() == 0) {
        // 没有配网记录，进配网模式
        startConfigPortal();
    } else {
        // 尝试连 WiFi（阻塞 10 秒看能不能连上）
        WiFi.setAutoReconnect(false);
        WiFi.begin(cfg_ssid.c_str(), cfg_pass.c_str());
        WiFi.waitForConnectResult(10000);  // 等 10 秒
        if (WiFi.status() != WL_CONNECTED) {
            // 连不上，密码/热点已变，清 NVS 进配网
            WiFi.disconnect(true);  // true = 清空 ESP32 内部 WiFi 配置
            clearAndRestart();
        } else {
            WiFi.setAutoReconnect(true);
        }
    }
}

void WiFiManager::startConfigPortal() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    dns.start(DNS_PORT, "*", WiFi.softAPIP());

    server.on("/", handleRoot);
    server.on("/save", handleSave);
    server.onNotFound(handleNotFound);
    server.begin();

    // 仅在此模式下阻塞，等配网完成
    unsigned long start = millis();
    while (true) {
        dns.processNextRequest();
        server.handleClient();
        delay(10);
        // 超时 10 分钟自动重启
        if (millis() - start > 600000) {
            ESP.restart();
        }
    }
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool WiFiManager::needsConfig() {
    return cfg_ssid.length() == 0;
}

void WiFiManager::clearAndRestart() {
    nvs_handle_t nvs;
    if (nvs_open(nvs_namespace, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    cfg_ssid = "";
    delay(100);
    ESP.restart();
}

String WiFiManager::getECSAddress() {
    return cfg_ecs_addr;
}

int WiFiManager::getECSPort() {
    return cfg_ecs_port;
}

String WiFiManager::getLocalIP() {
    return WiFi.localIP().toString();
}

WiFiManager wifiManager;
