#include "wifi_manager.h"
#include "pins.h"
#include <string.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_log.h>
#include <esp_http_server.h>
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
        uint16_t port = 0;
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
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_cfg = {};
    strcpy((char *)ap_cfg.ap.ssid, "ESP32-Config");
    ap_cfg.ap.max_connection = 4;
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    esp_wifi_start();

    ESP_LOGI(TAG, "Config Mode - ESP32-Config");
    ESP_LOGI(TAG, "Connect to WiFi 'ESP32-Config', open http://192.168.4.1");

    // Minimal HTTP server for config form
    static httpd_handle_t server = NULL;
    if (server) return;

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 3;
    http_cfg.server_port = 80;
    httpd_start(&server, &http_cfg);

    httpd_uri_t uri_root = {
        .uri = "/", .method = HTTP_GET,
        .handler = [](httpd_req_t *r) -> esp_err_t {
            const char *html = R"HTML(
<!DOCTYPE html><html><head><meta charset='utf-8'><title>ESP32 Config</title>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto}
input{width:100%;padding:8px;margin:8px 0}
button{width:100%;padding:10px;background:#07c;color:#fff;border:none;font-size:16px}
</style></head><body>
<h2>ESP32 WiFi Config</h2>
<form action='/save' method='post'>
<label>WiFi SSID</label><input name='ssid' required><br>
<label>Password</label><input name='pass' type='password'><br>
<button type='submit'>Save & Restart</button>
</form></body></html>)HTML";
            httpd_resp_send(r, html, strlen(html));
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_root);

    httpd_uri_t uri_save = {
        .uri = "/save", .method = HTTP_POST,
        .handler = [](httpd_req_t *r) -> esp_err_t {
            char buf[512] = {};
            httpd_req_recv(r, buf, sizeof(buf) - 1);
            char ssid[64] = {}, pass[128] = {};
            // Parse: ssid=xxx&pass=yyy
            if (sscanf(buf, "ssid=%63[^&]&pass=%127s", ssid, pass) >= 1) {
                // URL decode in-place
                auto url_decode = [](char *s) {
                    char *d = s;
                    while (*s) {
                        if (*s == '+') { *d++ = ' '; s++; }
                        else if (*s == '%' && *(s+1) && *(s+2)) {
                            char h[3] = {s[1], s[2], 0};
                            *d++ = strtol(h, NULL, 16);
                            s += 3;
                        } else { *d++ = *s++; }
                    }
                    *d = 0;
                };
                url_decode(ssid);
                url_decode(pass);
                nvs_handle_t nvs;
                if (nvs_open(NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
                    nvs_set_str(nvs, "ssid", ssid);
                    nvs_set_str(nvs, "pass", pass);
                    nvs_set_str(nvs, "ecs_addr", "118.31.46.156");
                    nvs_set_u16(nvs, "ecs_port", 8765);
                    nvs_commit(nvs);
                    nvs_close(nvs);
                }
                httpd_resp_sendstr(r, "OK, restarting...");
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            }
            httpd_resp_sendstr(r, "Invalid input");
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_save);
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
