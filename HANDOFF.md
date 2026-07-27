# Handoff — ESP32 Desktoppy Arduino Migration 完成

## 最终状态

v86 固件，Arduino 框架，基础管线全通：

| 模块 | 方案 | 状态 |
|------|------|------|
| OLED | U8g2 SSD1306 HW I2C | ✅ 正常显示 |
| WiFi | WiFiManager captive portal | ✅ "ESP32-Config" AP |
| WebSocket | links2004 WebSocketsClient | ✅ 收发全通 + ack |
| OTA | Arduino httpUpdate | ✅ 从 ECS HTTP server 拉 |
| I2S | 老 API i2s_driver_install | ✅ 连续流不卡网 |
| 按键录音 | GPIO 0 INPUT_PULLUP | ✅ 按住→WS 二进制帧上传 |

## 当前固件

- v86 commit `957e34f`
- 仓库：`Luaphes/Desktoppy`（main 分支）
- platformio.ini：`esp32-c3-supermini` 环境，`framework = arduino`
- 按键录音逻辑：`main.cpp` loop() 里 `btnHeld` 状态机
- WS 服务端：`ws_server.py`（支持二进制帧，保存为 `/tmp/esp32_audio_*.raw`）

## 下一阶段（新 session #92）

1. OTA v86 到 ESP32
2. 按住 GPIO 0 对麦克风说话 10s，松手
3. ECS 上确认 `/tmp/esp32_audio_*.raw` 有数据
4. STT：16kHz 16bit PCM raw → 语音识别（Whisper / 飞书 STT / DeepSeek）
5. Agent 推理：用户语音意图 → LLM 处理 → 生成回复文本
6. 回复下发：`{"type":"display","text":"..."}` 到 ESP32 OLED
7. TTS 发声：文本 → 语音合成 → 下发给 ESP32 喇叭播放

## 关键路径

- OTA 固件源：`https://github.com/Luaphes/Desktoppy/releases/download/vXX/firmware.bin`
- ghproxy 镜像（ECS 下不动 GitHub CDN 时）：`https://ghproxy.net/https://github.com/...`
- WS 命令格式：`{"type":"ota","url":"http://118.31.46.156:23717/firmware.bin"}`
- 音频格式：16kHz 16bit mono PCM raw（无 header）

## 注意事项

- 不要在 WS 回调里做阻塞 I2S 操作——用标志位 + loop() 模式
- OTA 固件先下载到 ECS（`/root/esp32-firmware/firmware.bin`），用 HTTP server 23717 端口分发
- ws_server 重启命令：`fuser -k 8765/tcp` 然后 `cd /root/esp32-firmware && python3 ws_server.py`
- ESP32 GPIO：0=按键, 2=WS, 3=SCK, 4=SD, 8=SCL, 10=SDA
