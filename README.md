# Desktoppy

Desktoppy 是一个以桌宠形态存在的 Agent 硬件终端。它在桌面端收音，把语音上传到 ECS，由线上 Agent 执行任务或对话，再把结果和通知回显到设备屏幕，形成一次完整消息闭环。

## 当前验证目标（M0）

1. ESP32-C3 长时间保持 Wi-Fi 与 WebSocket 在线。
2. 按键录音期间先将 16 kHz G.711 mu-law 写入本地 Flash，松手后上传，ECS 解码为单声道 16-bit PCM。
3. ECS 能向 OLED 下发文字和 OTA 指令。
4. 新固件只有在 Wi-Fi 与 WebSocket 连续在线 60 秒后才确认有效，否则保留回滚机会。

当前还没有接入 STT、LLM/Agent 和 TTS；服务端收到的音频只保存为原始 PCM。Agent 消息闭环属于下一阶段。

## 当前链路

```text
GPIO1 按键 → INMP441 → ESP32-C3 本地录音 → WebSocket :8765 → ECS despod
                                                       ├─ mu-law → 16-bit PCM
                                              ├─ OLED display 指令
                                              └─ OTA firmware 指令

ECS :23717 → firmware.bin → ESP32 OTA 双分区
```

ECS 服务：

- `despod.service`：WebSocket 与音频中继。
- `despod-firmware.service`：OTA 固件下载，systemd 自动恢复。

## 本地构建

```bash
pio run
pio run -t upload --upload-port /dev/cu.usbmodem14301
pio device monitor --port /dev/cu.usbmodem14301 --baud 115200
```

固件版本来自 `version.txt`，当前为 `v0.0.103`。启动日志、OLED 角标和 ECS 身份上报使用同一版本号。当前硬件只支持 2.4 GHz Wi-Fi；不要依赖 5 GHz 同名网络的频段引导。

## 产品路线

- M0：稳定在线、本地录音后上传、OLED 回显、可靠 OTA。
- M1：接入 STT → Agent/Chatbox → 屏幕通知，完成消息闭环。
- M2：更换适合双工音频的硬件并增加扬声器/TTS。
- M3：PCB、电源、3D 打印外壳、设备身份与安全 OTA，进入商品化验证。

接线请见 `wiring-guide.md`，当前按键使用 GPIO1，避免 GPIO0 启动绑带脚。
