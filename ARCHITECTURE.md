# Desktoppy 架构与演进边界

## 1. 产品目标

Desktoppy 不是单独的录音设备，而是一个桌宠形态的 Agent 终端：

```text
用户说话
  → 桌宠本地收音
  → ECS 转写并驱动 Agent / Chatbox
  → Agent 完成对话或任务
  → 结果、状态或通知回到桌宠屏幕
```

后续增加扬声器后，输出可以从屏幕扩展为 TTS。功能闭环验证完成后，再进入外壳、电源、PCB 和售卖形态设计。

## 2. 当前验证架构（M0）

```text
ESP32-C3
  ├─ GPIO1 按键
  ├─ INMP441 / I2S（仅录音时启动）
  ├─ SPIFFS 临时录音（松手后上传）
  ├─ SSD1315 OLED
  └─ WebSocket
        │
        ▼
ECS despod.service :8765
  ├─ 接收 G.711 mu-law 并解码为 16-bit PCM
  ├─ 下发 display / mic_test / ota
  └─ systemd 自动重启 + journald

ECS despod-firmware.service :23717
  └─ 提供 firmware.bin
        │
        ▼
ESP32 OTA 双分区 + 延迟确认 + 回滚
```

### 已实现

- Wi-Fi 配网与自动重连；配网热点不再自动超时退出。
- 本地 mu-law 录音、松手后 WebSocket 上传、ECS PCM 解码、OLED 文字回显。
- I2S 按需启停、30 秒录音上限、低频 OLED 音量刷新。
- HTTP/HTTPS OTA、双分区、下载进度和 systemd 固件服务。
- 新固件连续在线 60 秒后才标记有效。

### 尚未实现

- STT、Agent/Chatbox 调用、任务状态模型和结果摘要。
- 设备身份、鉴权、多设备稳定寻址。
- 音频文件进入 Agent 的正式队列与回调协议。
- 扬声器、TTS 与全双工音频。

## 3. 下一阶段（M1）：Agent 消息闭环

建议先保持 WebSocket，不急于引入 MQTT。单设备闭环的最小服务链路为：

```text
本地录音完成 → mic_start(codec=mulaw) / audio / mic_stop
  → 生成 recording_id
  → STT
  → Agent job
  → progress / result / error
  → OLED 展示
```

M1 必须先定义：

- 一次录音对应一个 `recording_id` 和一个 Agent `job_id`。
- Agent 状态至少包含 `received / transcribing / working / done / failed`。
- OLED 只展示短状态和最终摘要；长内容保留在 ECS/Chatbox。
- 网络中断后允许查询最后一个任务状态，不能依赖一次性推送。

## 4. 后续阶段

### M2：声音输出

增加扬声器和 TTS 前先确认硬件是否需要换为 ESP32-S3。目标是播放结果提示和短回复，不在当前 C3 原型上强行实现全双工。

### M3：商品化

- 定制 PCB 与可靠按键，避免使用启动绑带脚。
- 3D 打印外壳、散热、麦克风声学腔体和扬声器位置。
- USB-C 供电、电池保护和充电安全验证。
- 设备唯一身份、配网安全、TLS、固件签名和 OTA 灰度/回滚。
- 量产烧录、出厂测试、售后诊断和版本追踪。

## 5. 当前明确不做

- 不为单台验证设备提前引入 MQTT/Broker。
- 不在 STT/Agent 闭环完成前投入完整工业设计。
- 不把纯 HTTP、无设备鉴权的验证链路直接作为销售版本。
