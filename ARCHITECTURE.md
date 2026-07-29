# Desktoppy 后端架构演进路径

## 当前 (v1)

```
ESP32-C3 ──WebSocket──→ ws_server.py (nohup, nohup.out)
                            │
                  /tmp/ws_cmd.json (文件传命令)
                  /tmp/esp32_audio_*.raw
```

**痛点：** 进程无声挂掉、日志缓冲丢失、命令靠文件传、无健康检查。

---

## v2 — 稳定性治理（当前目标）

```
ESP32-C3 ──WebSocket──→ despod (systemd 托管)
                             │
                   REST API (POST /cmd/ota 等)
                   journald (实时日志，不缓冲)
                   │
                   └── OTA 固件: ESP32 WiFiClientSecure → GitHub CDN 直连
```

**改动：**
- systemd unit：自动重启、journald 日志
- aiohttp：REST API 替代 `/tmp/ws_cmd.json`
- 单文件 ~300 行

**收益：** 不再无声挂掉、命令有返回值、日志可追溯。

---

## v3 — 多设备 + MQTT（需要时触发）

```
ESP32 #1 ──┐
ESP32 #2 ──┼──MQTT──→ Mosquitto Broker ──→ despod ──→ STT/LLM/TTS
ESP32 #3 ──┘               │
                      pub/sub 解耦
                      多服务可独立订阅
```

**触发条件：**
- 2 台以上 ESP32 同时在线
- 需要 QoS 保证消息不丢
- 多个服务（日志/监控/STT）需要各自订阅同一路音频

---

## v4 — 全双工语音（需要换芯片）

```
ESP32-S3 ──MQTT──→ Broker ──→ STT → LLM → TTS ──→ Broker ──→ ESP32-S3
  双 I2S                                          MP3 音频流
  同时录音+播放
```

**触发条件：**
- 需要 ESP32 喇叭播放 TTS 回复
- 需要"对话感"而非"录音→等待→显示文字"

**硬件变更：** ESP32-C3 → ESP32-S3（双 I2S 外设）

---

## 技术选型决策记录

| 决策点 | 选择 | 为什么 |
|--------|------|--------|
| 协议 | WebSocket (v2) → MQTT (v3+) | 单设备用 WS 简单，多设备切 MQTT 才有收益 |
| 框架 | aiohttp | 比 FastAPI 轻（单包），WS 一等公民，迁移成本低 |
| 部署 | systemd | 自动重启 + journald，ECS 原生支持 |
| Broker | Mosquitto (v3+) | 比 EMQX 轻 100 倍，单机够用 |
| 芯片 | C3 (v2) → S3 (v4) | C3 单 I2S 够录，S3 双 I2S 才能录+放 |

---

## 不做的事

- ❌ 不用 FastAPI：加 uvicorn/starlette/pydantic 三个依赖，对单设备场景过重
- ❌ 不用 Railway/Serverless：ECS 已付费，迁移无收益
- ❌ 不现在上 MQTT：一台设备用 pub/sub 是杀鸡用牛刀
- ❌ 不换芯片：C3 录音够用，等需要喇叭播放时再换 S3
