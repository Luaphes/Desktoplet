#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebSocketsClient.h>
#include <HTTPUpdate.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <SPIFFS.h>
#include <esp_ota_ops.h>
#include <U8g2lib.h>
#include <driver/i2s.h>
#include "hermes_logo.h"

// ---- OLED (SSD1315 via U8g2 HW I2C) ----
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

// ---- WebSocket ----
WebSocketsClient ws;
bool micTestActive = false;
unsigned long micTestEnd = 0;

// ---- Deferred OTA (processed in loop, not WS callback) ----
bool otaPending = false;
String otaUrl = "";
bool firmwareValidated = false;
unsigned long firmwareStableSince = 0;

// ---- OLED burn-in prevention: corner rotation ----
static int g_verCorner = 3;              // 0=TL 1=TR 2=BL 3=BR
static unsigned long g_verNextJump = 0;

const char *ECS_HOST = "118.31.46.156";
const uint16_t ECS_PORT = 8765;
const char *OTA_PATH = "/firmware.bin";

// ---- I2S (INMP441, old API) ----
#define I2S_WS   2
#define I2S_SCK  3
#define I2S_SD   4
#define BTN_PIN  1

static bool i2sInstalled = false;
static bool i2sActive = false;
static int32_t rawAudioBuffer[512];
static int16_t pcmAudioBuffer[512];
static uint8_t encodedAudioBuffer[1024];
static size_t encodedAudioSamples = 0;
static uint32_t recordingBytesCaptured = 0;
static const char *RECORDING_PATH = "/recording.mulaw";
static File recordingFile;
static File uploadFile;

enum class AudioUploadState {
    IDLE,
    START,
    DATA,
    STOP,
};
static AudioUploadState audioUploadState = AudioUploadState::IDLE;

bool flushAudioBuffer() {
    if (encodedAudioSamples == 0) return true;
    size_t written = recordingFile
        ? recordingFile.write(encodedAudioBuffer, encodedAudioSamples)
        : 0;
    recordingBytesCaptured += written;
    bool stored = written == encodedAudioSamples;
    encodedAudioSamples = 0;
    return stored;
}

uint8_t linearToMuLaw(int16_t pcm) {
    constexpr int32_t BIAS = 0x84;
    constexpr int32_t CLIP = 32635;
    int32_t sample = pcm;
    uint8_t sign = 0;
    if (sample < 0) {
        sign = 0x80;
        sample = -sample;
    }
    if (sample > CLIP) sample = CLIP;
    sample += BIAS;

    uint8_t exponent = 7;
    for (int32_t mask = 0x4000; exponent > 0 && !(sample & mask);
         exponent--, mask >>= 1) {}
    uint8_t mantissa = (sample >> (exponent + 3)) & 0x0F;
    return ~(sign | (exponent << 4) | mantissa);
}

void queueAudioSamples(const int16_t *samples, size_t count) {
    for (size_t i = 0; i < count; i++) {
        encodedAudioBuffer[encodedAudioSamples++] = linearToMuLaw(samples[i]);
        // G.711 mu-law keeps 16 kHz speech at 16 KB/s. Capture goes to local
        // flash first so WAN latency can never block the button or OLED.
        if (encodedAudioSamples == sizeof(encodedAudioBuffer)) {
            flushAudioBuffer();
        }
    }
}

void restartAudioUpload() {
    if (uploadFile) uploadFile.close();
    ws.disconnect();
    audioUploadState = AudioUploadState::START;
}

void processAudioUpload() {
    if (audioUploadState == AudioUploadState::IDLE || !ws.isConnected()) return;

    if (audioUploadState == AudioUploadState::START) {
        if (uploadFile) uploadFile.close();
        uploadFile = SPIFFS.open(RECORDING_PATH, FILE_READ);
        if (!uploadFile) {
            Serial.println("[UPLOAD] recording file missing");
            audioUploadState = AudioUploadState::IDLE;
            return;
        }
        String start = String("{\"type\":\"mic_start\",\"codec\":\"mulaw\",") +
                       "\"sample_rate\":16000,\"stored\":true}";
        if (!ws.sendTXT(start)) {
            restartAudioUpload();
            return;
        }
        Serial.printf("[UPLOAD] start bytes=%u\n", (unsigned)uploadFile.size());
        audioUploadState = AudioUploadState::DATA;
        return;
    }

    if (audioUploadState == AudioUploadState::DATA) {
        uint8_t chunk[1024];
        size_t bytes = uploadFile.read(chunk, sizeof(chunk));
        if (bytes == 0) {
            audioUploadState = AudioUploadState::STOP;
            return;
        }
        if (!ws.sendBIN(chunk, bytes)) {
            restartAudioUpload();
        }
        return;  // exactly one network write per main-loop pass
    }

    if (audioUploadState == AudioUploadState::STOP) {
        if (!ws.sendTXT("{\"type\":\"mic_stop\",\"stored\":true}")) {
            restartAudioUpload();
            return;
        }
        size_t uploaded = uploadFile.size();
        uploadFile.close();
        SPIFFS.remove(RECORDING_PATH);
        audioUploadState = AudioUploadState::IDLE;
        Serial.printf("[UPLOAD] complete wire_bytes=%u\n", (unsigned)uploaded);
    }
}

bool startI2S() {
    if (i2sActive) return true;

    if (i2sInstalled) {
        if (i2s_start(I2S_NUM_0) != ESP_OK) return false;
        i2sActive = true;
        return true;
    }

    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        // INMP441 puts one signed 24-bit sample in a 32-bit I2S slot.
        // Reading it as 16-bit produced alternating zeros/full-scale values.
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        // Keep 512 ms of RX capacity so a WAN WebSocket write cannot starve
        // the microphone DMA ring.
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = false,
    };
    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL) != ESP_OK) {
        return false;
    }
    i2s_pin_config_t pin = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD,
    };
    if (i2s_set_pin(I2S_NUM_0, &pin) != ESP_OK) {
        i2s_driver_uninstall(I2S_NUM_0);
        return false;
    }
    i2sInstalled = true;
    i2sActive = true;  // i2s_driver_install() starts the driver.
    return true;
}

void stopI2S() {
    if (!i2sInstalled) return;
    if (i2sActive) i2s_stop(I2S_NUM_0);
    // A stopped driver still owns DMA/interrupt resources on the C3. Keeping
    // it installed made the Wi-Fi data path fail shortly after recording.
    i2s_driver_uninstall(I2S_NUM_0);
    i2sActive = false;
    i2sInstalled = false;
}

bool jsonStringValue(const String &json, const char *key, String &value) {
    String token = String("\"") + key + "\"";
    int pos = json.indexOf(token);
    if (pos < 0) return false;
    pos = json.indexOf(':', pos + token.length());
    if (pos < 0) return false;
    pos++;
    while (pos < (int)json.length() && isspace((unsigned char)json[pos])) pos++;
    if (pos >= (int)json.length() || json[pos] != '"') return false;
    pos++;
    int end = pos;
    while (end < (int)json.length()) {
        if (json[end] == '"' && (end == pos || json[end - 1] != '\\')) break;
        end++;
    }
    if (end >= (int)json.length()) return false;
    value = json.substring(pos, end);
    value.replace("\\\"", "\"");
    value.replace("\\n", "\n");
    return true;
}

// ---- Version display (burn-in safe: 30min corner rotation) ----
void drawVersionCorner() {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    int tw = u8g2.getStrWidth(FIRMWARE_VERSION);
    int x, y;
    switch (g_verCorner) {
        case 0: x = 2;               y = 8;  break;  // TL
        case 1: x = 128 - tw - 2;    y = 8;  break;  // TR
        case 2: x = 2;               y = 64; break;  // BL
        case 3: x = 128 - tw - 2;    y = 64; break;  // BR
    }
    u8g2.drawStr(x, y, FIRMWARE_VERSION);
}

// ---- OTA progress callback ----
void otaProgress(int cur, int total) {
    if (total <= 0) return;
    int pct = (cur * 100) / total;
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 12, "OTA Update");
    u8g2.drawFrame(10, 24, 108, 16);
    int fw = pct * 104 / 100;
    if (fw > 0) u8g2.drawBox(12, 26, fw, 12);
    char buf[16];
    sprintf(buf, "%d%%", pct);
    u8g2.setCursor((128 - u8g2.getStrWidth(buf)) / 2, 56);
    u8g2.print(buf);
    drawVersionCorner();
    u8g2.sendBuffer();
}

// ---- WS Event ----
void wsEvent(WStype_t type, uint8_t *data, size_t len) {
    if (type == WStype_CONNECTED) {
        Serial.println("[WS] connected");
        String identity = String("{\"type\":\"identify\",\"mac\":\"") +
                          WiFi.macAddress() + "\",\"version\":\"" +
                          FIRMWARE_VERSION + "\"}";
        ws.sendTXT(identity);
    } else if (type == WStype_DISCONNECTED) {
        Serial.println("[WS] disconnected");
    } else if (type == WStype_ERROR) {
        Serial.printf("[WS] error len=%u\n", (unsigned)len);
    } else if (type == WStype_TEXT) {
        String msg((char*)data, len);
        if (msg.indexOf("\"ota\"") >= 0) {
            String url;
            if (jsonStringValue(msg, "url", url)) {
                otaPending = true;
                otaUrl = url;
            }
        } else if (msg.indexOf("\"display\"") >= 0) {
            String text = "";
            jsonStringValue(msg, "text", text);
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr);
            int tw = u8g2.getStrWidth(text.c_str());
            u8g2.drawStr((128 - tw) / 2, 32, text.c_str());
            drawVersionCorner();
            u8g2.sendBuffer();
        } else if (msg.indexOf("\"mic_test\"") >= 0) {
            // MIC test: set flag, main loop handles to avoid blocking WS
            int dur = 5;
            int d = msg.indexOf("\"duration\":");
            if (d >= 0) dur = msg.substring(d + 11).toInt();
            micTestEnd = millis() + dur * 1000;
            micTestActive = true;
        }
        ws.sendTXT("{\"type\":\"ack\"}");
    }
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    delay(100);
    Serial.printf("[BOOT] firmware=%s reset=%d heap=%u\n",
                  FIRMWARE_VERSION, (int)esp_reset_reason(), ESP.getFreeHeap());
    if (!SPIFFS.begin(true)) {
        Serial.println("[FS] SPIFFS mount failed");
    } else {
        Serial.printf("[FS] ready total=%u used=%u\n",
                      (unsigned)SPIFFS.totalBytes(), (unsigned)SPIFFS.usedBytes());
    }

    // Boot animation — Hermes logo (64x64) + Desktoppy label
    u8g2.begin();
    u8g2.setFlipMode(0);
    u8g2.clearBuffer();
    u8g2.drawXBM(0, 0, 64, 64, hermes_logo);
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(70, 30, "Desktoppy");
    drawVersionCorner();
    u8g2.sendBuffer();
    delay(1500);

    // ---- Hold BTN 3s to reset WiFi ----
    pinMode(BTN_PIN, INPUT_PULLUP);
    {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(0, 20, "Hold BTN 3s");
        u8g2.drawStr(0, 36, "to reset WiFi");
        drawVersionCorner();
        u8g2.sendBuffer();

        unsigned long t0 = millis();
        bool resetWiFi = false;
        while (millis() - t0 < 5000) {
            if (digitalRead(BTN_PIN) == LOW) {
                unsigned long held = millis();
                while (digitalRead(BTN_PIN) == LOW) {
                    if (millis() - held > 3000) { resetWiFi = true; break; }
                    delay(10);
                }
                if (resetWiFi) break;
            }
            delay(10);
        }
        if (resetWiFi) {
            WiFiManager wmReset;
            wmReset.resetSettings();
            u8g2.clearBuffer();
            u8g2.drawStr(0, 28, "WiFi Reset OK");
            drawVersionCorner();
            u8g2.sendBuffer();
            delay(2000);
            ESP.restart();
        }
    }

    // WiFi
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 20, "Connecting...");
    drawVersionCorner();
    u8g2.sendBuffer();
    
    // Apply radio reliability settings before the WPA handshake, not after it.
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.printf("[WIFI] disconnected reason=%u\n",
                      info.wifi_sta_disconnected.reason);
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    WiFiManager wm;
    wm.setConfigPortalTimeout(0);  // stay available until provisioning succeeds
    wm.setConnectTimeout(20);
    wm.setConnectRetries(10);
    wm.setWiFiAutoReconnect(true);
    wm.setAPCallback([](WiFiManager *) {
        Serial.println("[WIFI] config portal ready: ESP32-Config 192.168.4.1");
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(0, 16, "ESP32-Config");
        u8g2.drawStr(0, 34, "192.168.4.1");
        drawVersionCorner();
        u8g2.sendBuffer();
    });
    Serial.println("[WIFI] autoConnect start");
    if (!wm.autoConnect("ESP32-Config")) {
        Serial.println("[WIFI] autoConnect failed; restarting");
        ESP.restart();
    }
    // Radio power saving remains disabled during validation.
    Serial.printf("[WIFI] connected ip=%s rssi=%d\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());

    // WiFiManager saves credentials internally, no need for manual prefs

    // Display status
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    int tw = u8g2.getStrWidth("WiFi OK");
    u8g2.drawStr((128 - tw) / 2, 14, "WiFi OK");
    String ip = WiFi.localIP().toString();
    int iw = u8g2.getStrWidth(ip.c_str());
    u8g2.drawStr((128 - iw) / 2, 34, ip.c_str());
    drawVersionCorner();
    u8g2.sendBuffer();

    // I2S is started only while recording. Continuous idle DMA caused the
    // ESP32-C3 to lose WiFi/reboot after roughly 30-80 seconds.
    pinMode(BTN_PIN, INPUT_PULLUP);

    // WebSocket
    ws.begin(ECS_HOST, ECS_PORT, "/");
    ws.onEvent(wsEvent);
    ws.setReconnectInterval(3000);

}

void loop() {
    if (WiFi.status() == WL_CONNECTED) ws.loop();

    static unsigned long lastHealthLog = 0;
    static unsigned long wsOfflineSince = 0;
    static unsigned long lastNetworkRecovery = 0;
    if (millis() - lastHealthLog >= 10000UL) {
        Serial.printf("[HEALTH] wifi=%d ws=%d rssi=%d heap=%u\n",
                      (int)WiFi.status(), ws.isConnected() ? 1 : 0,
                      WiFi.RSSI(), ESP.getFreeHeap());
        lastHealthLog = millis();
    }
    if (WiFi.status() == WL_CONNECTED && !ws.isConnected()) {
        if (wsOfflineSince == 0) wsOfflineSince = millis();
        if (millis() - wsOfflineSince >= 30000UL &&
            millis() - lastNetworkRecovery >= 30000UL) {
            Serial.println("[WIFI] resetting STA after 30s WS outage");
            WiFi.disconnect(false, false);
            delay(100);
            WiFi.reconnect();
            lastNetworkRecovery = millis();
            wsOfflineSince = millis();
        }
    } else {
        wsOfflineSince = 0;
    }

    // Confirm an OTA image only after the full online path stays healthy.
    // Until then, a reboot preserves the bootloader's rollback opportunity.
    if (!firmwareValidated) {
        if (WiFi.status() == WL_CONNECTED && ws.isConnected()) {
            if (firmwareStableSince == 0) firmwareStableSince = millis();
            if (millis() - firmwareStableSince >= 60000UL) {
                esp_ota_mark_app_valid_cancel_rollback();
                firmwareValidated = true;
                Serial.println("[OTA] firmware marked valid after 60s online");
            }
        } else {
            firmwareStableSince = 0;
        }
    }
    
    // ---- Deferred OTA (non-blocking flag, actual download here) ----
    if (otaPending) {
        stopI2S();
        t_httpUpdate_return ret = HTTP_UPDATE_FAILED;
        for (int attempt = 1; attempt <= 3 && ret == HTTP_UPDATE_FAILED; attempt++) {
            Serial.printf("[OTA] download attempt %d/3\n", attempt);
            HTTPUpdate updater;
            updater.rebootOnUpdate(false);
            updater.onProgress(otaProgress);
            if (otaUrl.startsWith("https://")) {
                WiFiClientSecure client;
                client.setInsecure();  // trusted release URL; certificate is not pinned yet
                ret = updater.update(client, otaUrl);
            } else {
                WiFiClient client;
                ret = updater.update(client, otaUrl);
            }
            if (ret == HTTP_UPDATE_FAILED && attempt < 3) delay(1000);
        }
        if (ret == HTTP_UPDATE_OK) {
            Serial.println("[OTA] update written; restarting cleanly");
            delay(500);
            ESP.restart();
        } else {
            const char *err = (ret == HTTP_UPDATE_FAILED) ? "OTA Failed" : "No Update";
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr);
            u8g2.drawStr(0, 28, err);
            drawVersionCorner();
            u8g2.sendBuffer();
            delay(3000);
        }
        otaPending = false;
        otaUrl = "";
    }

    // Stored recordings upload after I2S has stopped. Each pass sends at most
    // one small frame, so control messages and the UI remain responsive.
    processAudioUpload();
    
    // 30min corner rotation (OLED burn-in prevention)
    if (millis() - g_verNextJump > 30 * 60 * 1000UL) {
        g_verCorner = (g_verCorner + 1) % 4;
        g_verNextJump = millis();
    }
    
    // Button-triggered I2S streaming
    static bool btnHeld = false;
    static bool buttonArmed = false;
    static bool recordingTimedOut = false;
    static bool lastPressed = false;
    static unsigned long btnDown = 0;
    static unsigned long recordingStarted = 0;
    static unsigned long lastVolumeDraw = 0;
    static unsigned long lastWifiRetry = 0;
    static uint32_t startupSamplesToDrop = 0;
    bool pressed = (digitalRead(BTN_PIN) == LOW);

    if (pressed != lastPressed) {
        Serial.printf("[BTN] %s\n", pressed ? "down" : "up");
        lastPressed = pressed;
    }

    // A button held during boot must be released once before it can record.
    if (!pressed) buttonArmed = true;

    if (WiFi.status() != WL_CONNECTED) {
        if (btnHeld) {
            Serial.println("[MIC] stream stop: WiFi lost");
            flushAudioBuffer();
            if (recordingFile) recordingFile.close();
            stopI2S();
            btnHeld = false;
            btnDown = 0;
            recordingTimedOut = true;
            if (recordingBytesCaptured > 0) {
                audioUploadState = AudioUploadState::START;
            }
        }
        // A WPA attempt can take around 20 seconds. Retrying every 3 seconds
        // cancels the in-flight attempt and traps the station offline.
        if (lastWifiRetry == 0 || millis() - lastWifiRetry >= 30000UL) {
            Serial.printf("[WIFI] reconnect status=%d\n", (int)WiFi.status());
            WiFi.reconnect();
            lastWifiRetry = millis();
        }
        delay(10);
        return;
    }
    
    if (pressed && buttonArmed && !recordingTimedOut && !btnHeld &&
        audioUploadState == AudioUploadState::IDLE && btnDown == 0) {
        btnDown = millis();
    } else if (pressed && buttonArmed && !recordingTimedOut && !btnHeld &&
               audioUploadState == AudioUploadState::IDLE &&
               millis() - btnDown > 100) {
        SPIFFS.remove(RECORDING_PATH);
        recordingFile = SPIFFS.open(RECORDING_PATH, FILE_WRITE);
        if (recordingFile && startI2S()) {
            btnHeld = true;
            recordingStarted = millis();
            recordingBytesCaptured = 0;
            encodedAudioSamples = 0;
            startupSamplesToDrop = 4096;  // first 256 ms
            lastVolumeDraw = 0;
            Serial.println("[MIC] stream start");
        } else {
            if (recordingFile) recordingFile.close();
            btnDown = 0;
        }
    } else if (!pressed) {
        if (btnHeld) {
            flushAudioBuffer();
            if (recordingFile) recordingFile.close();
            Serial.printf("[MIC] stream stop: button released ms=%lu stored_bytes=%u\n",
                          millis() - recordingStarted, recordingBytesCaptured);
            stopI2S();
            if (recordingBytesCaptured > 0) {
                audioUploadState = AudioUploadState::START;
            }
            // Restore WiFi OK display
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr);
            int tw3 = u8g2.getStrWidth("WiFi OK");
            u8g2.drawStr((128 - tw3) / 2, 14, "WiFi OK");
            String rip = WiFi.localIP().toString();
            int iw3 = u8g2.getStrWidth(rip.c_str());
            u8g2.drawStr((128 - iw3) / 2, 34, rip.c_str());
            drawVersionCorner();
            u8g2.sendBuffer();
        }
        btnHeld = false;
        btnDown = 0;
        recordingTimedOut = false;
    }

    // Never allow a stuck button to create an unbounded audio stream or block OTA.
    if (btnHeld && millis() - recordingStarted >= 15000UL) {
        flushAudioBuffer();
        if (recordingFile) recordingFile.close();
        Serial.println("[MIC] stream stop: 15s timeout");
        stopI2S();
        btnHeld = false;
        btnDown = 0;
        recordingTimedOut = true;
        if (recordingBytesCaptured > 0) {
            audioUploadState = AudioUploadState::START;
        }
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        int tw = u8g2.getStrWidth("WiFi OK");
        u8g2.drawStr((128 - tw) / 2, 14, "WiFi OK");
        String ip = WiFi.localIP().toString();
        int iw = u8g2.getStrWidth(ip.c_str());
        u8g2.drawStr((128 - iw) / 2, 34, ip.c_str());
        drawVersionCorner();
        u8g2.sendBuffer();
    }
    
    if (btnHeld) {
        size_t bytes = 0;
        i2s_read(I2S_NUM_0, rawAudioBuffer, sizeof(rawAudioBuffer), &bytes,
                 pdMS_TO_TICKS(50));
        int rawSamples = bytes / sizeof(rawAudioBuffer[0]);
        int firstSample = 0;

        // The INMP441 produces a large transient as its I2S clock starts.
        // Drop the first 256 ms without coupling this to the DMA read size.
        if (startupSamplesToDrop > 0 && rawSamples > 0) {
            uint32_t drop = min((uint32_t)rawSamples, startupSamplesToDrop);
            firstSample = drop;
            startupSamplesToDrop -= drop;
        }
        int samples = rawSamples - firstSample;
        for (int i = 0; i < samples; i++) {
            // Drop the unused low bits and add roughly 12 dB of digital gain.
            int32_t sample = rawAudioBuffer[firstSample + i] >> 14;
            if (sample > INT16_MAX) sample = INT16_MAX;
            if (sample < INT16_MIN) sample = INT16_MIN;
            pcmAudioBuffer[i] = (int16_t)sample;
        }
        if (samples > 0) {
            // Encode speech to 8-bit G.711 mu-law and store locally. The ECS
            // expands it back to 16-bit PCM after the button is released.
            queueAudioSamples(pcmAudioBuffer, samples);
        }
        // OLED I2C is much slower than audio. Refresh at 10 Hz so it cannot
        // starve I2S reads and WebSocket uploads.
        if (samples > 0 && millis() - lastVolumeDraw >= 100) {
            int64_t sum = 0;
            for (int i = 0; i < samples; i++) {
                int32_t v = pcmAudioBuffer[i]; if (v < 0) v = -v;
                sum += v;
            }
            // 4,000 mean-absolute amplitude maps to full scale. This keeps
            // normal speech responsive without pinning the UI at 100%.
            int vol = (sum / samples) * 100 / 4000;
            if (vol > 100) vol = 100;
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr);
            u8g2.drawStr(0, 12, "REC");
            u8g2.drawFrame(10, 24, 108, 16);
            int fw = vol * 104 / 100;
            if (fw > 0) u8g2.drawBox(12, 26, fw, 12);
            char p[8]; sprintf(p, "%d%%", vol);
            u8g2.setCursor((128 - u8g2.getStrWidth(p)) / 2, 56);
            u8g2.print(p);
            drawVersionCorner();
            u8g2.sendBuffer();
            lastVolumeDraw = millis();
        }
    }
    
    // mic_test via WS command (non-blocking)
    if (micTestActive) {
        if (millis() >= micTestEnd) {
            micTestActive = false;
            if (!btnHeld) stopI2S();
            ws.sendTXT("{\"type\":\"mic_done\"}");
            // Restore WiFi OK display
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr);
            int tw2 = u8g2.getStrWidth("WiFi OK");
            u8g2.drawStr((128 - tw2) / 2, 14, "WiFi OK");
            String rip = WiFi.localIP().toString();
            int iw2 = u8g2.getStrWidth(rip.c_str());
            u8g2.drawStr((128 - iw2) / 2, 34, rip.c_str());
            drawVersionCorner();
            u8g2.sendBuffer();
        } else if (!btnHeld && startI2S()) {
            int16_t buf[64];
            size_t bytes = 0;
            i2s_read(I2S_NUM_0, buf, sizeof(buf), &bytes, 0);
            int samples = bytes / 2;
            int32_t sum = 0;
            for (int i = 0; i < samples; i++) {
                int32_t v = buf[i]; if (v < 0) v = -v;
                sum += v;
            }
            int vol = samples ? (sum / samples) * 100 / 256 : 0;
            if (vol > 100) vol = 100;
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr);
            u8g2.drawStr(0, 12, "MIC Test");
            u8g2.drawFrame(10, 24, 108, 16);
            int fw = vol * 104 / 100;
            if (fw > 0) u8g2.drawBox(12, 26, fw, 12);
            char p[8]; sprintf(p, "%d%%", vol);
            u8g2.setCursor((128 - u8g2.getStrWidth(p)) / 2, 56);
            u8g2.print(p);
            drawVersionCorner();
            u8g2.sendBuffer();
        }
    }
}
