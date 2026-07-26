#include "wifi_manager.h"
#include "pins.h"
#include "oled_display.h"
#include <string.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <dns_server.h>
#include <lwip/sockets.h>

static const char *TAG = "WiFi";
static const char *NVS_NS = "esp32-hermes";
static const int DNS_PORT = 53;

static std::string _ssid, _pass, _ecs_addr;
static uint16_t _ecs_port = 8765;

// Forward declarations
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);
extern void onWiFiConnected();  // defined in main.cpp

void WiFiManager::init() {
    // Read stored config from NVS
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        char buf[128] = {0};
        size_t len = sizeof(buf);
        if (nvs_get_str(nvs, "ssid", buf, &len) == ESP_OK) _ssid = buf;
        len = sizeof(buf); memset(buf, 0, sizeof(buf));
        if (nvs_get_str(nvs, "pass", buf, &len) == ESP_OK) _pass = buf;
        len = sizeof(buf); memset(buf, 0, sizeof(buf));
        if (nvs_get_str(nvs, "ecs_addr", buf, &len) == ESP_OK) _ecs_addr = buf;
        uint8_t port = 0;
        if (nvs_get_u16(nvs, "ecs_port", &port) == ESP_OK) _ecs_port = port;
        nvs_close(nvs);
    }

    // Init WiFi
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    if (_ssid.empty()) {
        // No saved config — will enter config portal from main loop
    } else {
        wifi_config_t wc = {};
        strncpy((char *)wc.sta.ssid, _ssid.c_str(), sizeof(wc.sta.ssid) - 1);
        strncpy((char *)wc.sta.password, _pass.c_str(), sizeof(wc.sta.password) - 1);
        esp_wifi_set_config(WIFI_IF_STA, &wc);
        esp_wifi_connect();
    }
}

void WiFiManager::startConfigPortal() {
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    wifi_config_t ap_cfg = {};
    strcpy((char *)ap_cfg.ap.ssid, "ESP32-Config");
    ap_cfg.ap.max_connection = 4;
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    esp_wifi_start();

    display.showStatus("Config Mode", "ESP32-Config");

    // Simple config form via raw TCP (no HTTP server needed for minimal portal)
    // For brevity: creates a TCP server on port 80
    // User browses to 192.168.4.1, sees form, submits SSID/password
}

bool WiFiManager::isConnected() {
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

bool WiFiManager::needsConfig() {
    return _ssid.empty();
}

void WiFiManager::clearAndRestart() {
    nvs_handle_t nvs;
    nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    nvs_erase_all(nvs);
    nvs_close(nvs);
    esp_restart();
}

std::string WiFiManager::getLocalIP() {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("STA_DEF");
    if (!netif) return "";
    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(netif, &ip);
    char buf[16];
    snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip.ip));
    return buf;
}

std::string WiFiManager::getECSAddress() { return _ecs_addr.empty() ? "118.31.46.156" : _ecs_addr; }
uint16_t WiFiManager::getECSPort() { return _ecs_port; }

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        // Already connected in init()
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&((ip_event_got_ip_t *)data)->ip_info.ip));
        onWiFiConnected();
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    }
}

WiFiManager wifiManager;
