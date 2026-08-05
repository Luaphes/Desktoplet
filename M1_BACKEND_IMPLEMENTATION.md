# Desktoppy M1 后端实现说明

> 状态：READY FOR ECS AGENT IMPLEMENTATION
>
> 本文件是给 ECS Agent 的执行约束，不是自由发挥的方案草稿。实现前先完整阅读；遇到契约冲突或无法满足的约束，先在 HANDOFF 中说明，不要擅自扩大范围。

## 0. 项目、分支和目录边界

- 项目名称：`Desktoppy`
- ECS 开发工程：`/root/Desktoppy`
- canonical 功能分支：`m1-stt-pipeline`
- ECS 线上运行目录：`/root/esp32-firmware`
- 当前稳定固件：`v0.0.103`
- 当前目标设备 MAC：`14:63:93:90:CF:94`

### 绝对边界

1. 所有 M1 源码、测试、systemd 草案和文档只写入 `/root/Desktoppy`。
2. `/root/esp32-firmware` 是线上 M0 运行目录；未经明确验收和授权，不替换其中的 `ws_server.py`，不重启 `despod.service`。
3. 不修改 ESP32 固件，不触发 OTA，不改变 `v0.0.103` 的发布文件和 OTA 服务。
4. 不把 API key、token、原始语音文本或私钥写入 Git、HANDOFF、普通日志或测试输出。
5. 不新增 Redis、RabbitMQ、Celery、Kubernetes、Docker 或多进程 worker。当前 ECS 约 2 vCPU、1.6 GiB 可见内存、40 GiB 磁盘，必须优先保持轻量。

## 1. 本阶段唯一目标

在不升级固件、不破坏 M0 WebSocket/OTA 的前提下，让一块真实 ESP32 完成：

```text
GPIO1 录音
  → 现有 WebSocket 上传
  → recording_id / job_id
  → SiliconFlow STT
  → Agent Gateway（带 session_id）
  → status / transcript / reply
  → 现有 WebSocket display
  → OLED
```

本阶段不是产品化，不做 TTS、不做多 ECS、不做多设备压测、不迁移 Railway。

## 2. 目标运行拓扑

```text
ESP32 v0.0.103
    │ WebSocket :8765
    ▼
M0 Device Gateway（现有 despod.service）
    │ 只对目标 MAC 开 canary
    │ localhost HTTP
    ▼
M1 Orchestrator（despod-m1.service）
    │ 127.0.0.1:8786
    ├─ SQLite job metadata
    ├─ bounded asyncio.Queue(maxsize=1)
    ├─ SiliconFlow STT adapter
    └─ Agent Gateway adapter
          │
          ▼
      display events → M0 Device Gateway → OLED
```

### Canary 定义

Canary 是发布策略，不是第三方软件。M0 Gateway 收到 ESP32 的 `identify` 后，按 MAC 判断是否启用 M1：

- 目标 MAC：调用 M1 Orchestrator；
- 其它设备：继续 M0 行为；
- M1 失败：发送简短错误提示，保持 WebSocket 和 OTA 服务存活。

ESP32 当前固定连接 `8765`，所以不要要求板子改连 `8766`。M1 初期只绑定 `127.0.0.1:8786`，不得打开公网入站端口。

## 3. FastAPI / Uvicorn 约束

- FastAPI：应用框架，定义 HTTP API、数据模型和健康检查。
- Uvicorn：ASGI 运行服务器，负责监听 `127.0.0.1:8786` 和执行 FastAPI。
- 只启动一个 Uvicorn 进程；systemd 负责启动、重启和日志。
- 不开 `--workers`；不在 M1 进程里加载本地模型。
- 外部 API 调用必须使用异步 HTTP 客户端或不会阻塞 WebSocket 接收循环的后台任务。

建议最小目录：

```text
/root/Desktoppy/m1_service/
  __init__.py
  app.py              # FastAPI app 和路由
  models.py           # job / event 数据模型
  store.py            # SQLite 读写
  worker.py           # 有界队列和状态机
  providers/
    stt.py            # SiliconFlow 适配器
    agent.py          # Agent Gateway 适配器
tests/
  test_m1_service.py
```

## 4. 内部 API 契约

### `GET /healthz`

只检查进程、SQLite 和队列，不调用 SiliconFlow 或 Agent。

成功返回 HTTP 200：

```json
{"status":"ok","service":"despod-m1","queue_depth":0}
```

### `POST /internal/v1/jobs`

由 M0 Gateway 在 `mic_stop` 且目标 MAC 命中 canary 后调用。请求体：

```json
{
  "device_id": "14:63:93:90:CF:94",
  "session_id": "device-14-63-93-90-CF-94",
  "recording_id": "rec-...",
  "audio_path": "relative/path/to/recording.raw",
  "codec": "mulaw",
  "sample_rate": 16000,
  "channels": 1
}
```

约束：

- `audio_path` 必须解析到配置的音频根目录内，拒绝 `..` 路径穿越。
- 不通过 HTTP body 传整段音频，不做 base64 音频。
- `recording_id` 必须幂等；同一 recording 重复提交返回原 job，不重复调用 STT/Agent。
- 任务先写入 SQLite，再进入 `asyncio.Queue(maxsize=1)`。
- 队列满时返回 HTTP 409，不丢任务、不无限堆积。

成功创建返回 HTTP 202：

```json
{
  "job_id": "job-...",
  "recording_id": "rec-...",
  "status": "received"
}
```

### `GET /internal/v1/jobs/{job_id}`

返回：

```json
{
  "job_id": "job-...",
  "recording_id": "rec-...",
  "device_id": "14:63:93:90:CF:94",
  "session_id": "device-14-63-93-90-CF-94",
  "status": "transcribing",
  "transcript": null,
  "reply": null,
  "error_code": null,
  "created_at": "...",
  "updated_at": "..."
}
```

## 5. 状态机和用户可见事件

状态只能按以下方向流转：

```text
received
  → transcribing
  → working
  → displaying
  → done
```

任何阶段都可以进入 `failed`；不得静默吞错。

M0 Gateway 至少向 OLED 发送：

- `处理中...`
- `没听清...`
- `你说：<transcript>`
- `Agent 回复的短摘要`
- `服务暂不可用`

OLED 文本必须限制在当前 C3 可显示范围内（最多约 4 行）；长回复截断或只展示摘要，不把大段文本推给板子。

## 6. STT 和 Agent 处理顺序

必须严格串行：

1. 读取并校验 PCM/mu-law 文件。
2. 调用 SiliconFlow STT。
3. 保存 transcript 到 job。
4. 以 `session_id + device_id + transcript` 调用 Agent Gateway。
5. 保存 Agent reply。
6. 把 status、transcript 和 reply 交给 M0 Gateway 发送 OLED。

Agent Gateway 负责会话上下文和产品记忆；M1 Orchestrator 不自行发明另一套记忆系统。Hermes/Git/HANDOFF 留痕属于开发协作，不进入产品实时链路。

## 7. 配置和安全

- SiliconFlow 与 Agent 的密钥只能来自 ECS 环境变量或受权限保护的 `EnvironmentFile`，不得写入仓库。
- HTTP 请求必须设置连接、读取和总超时；禁止无限等待。
- 外部 API 失败最多做有限重试，并记录不含 secret 的错误码。
- `/internal/*` 只监听 loopback；初期不对公网开放。
- 不打印原始 Authorization header、API key、完整音频内容或完整对话隐私文本。

## 8. systemd 草案

服务名：`despod-m1.service`

关键约束：

```ini
[Unit]
After=network-online.target

[Service]
WorkingDirectory=/root/Desktoppy
ExecStart=/usr/bin/python3 -m uvicorn m1_service.app:app --host 127.0.0.1 --port 8786
Restart=on-failure
RestartSec=3
EnvironmentFile=/etc/despod-m1.env

[Install]
WantedBy=multi-user.target
```

这是草案。ECS Agent 必须先用当前 Python 环境确认依赖，再决定是否使用虚拟环境；不要为了一个 FastAPI 服务安装重量级系统组件。

## 9. 验收顺序

### A. 不接板子

1. Python import、FastAPI 路由和 SQLite 初始化通过。
2. `GET /healthz` 返回 200。
3. 用历史 v103 音频样本完成 STT → Agent mock/真实 API 测试。
4. 重复 `recording_id` 不重复调用模型。
5. 队列满、超时、STT 失败、Agent 失败都有明确状态。

### B. 接入 canary 板子

1. 不升级固件，板子继续显示 v103。
2. `despod.service` 和 OTA 服务保持 active。
3. 目标 MAC 录音时收到 `处理中...`。
4. ECS 日志能看到 job 状态变化，但不泄露 key/完整语音。
5. OLED 收到 transcript 和 Agent reply。
6. 连续重复两次录音，第二次也能完成。
7. M1 停止或失败时，板子仍可回到 `wifiok`，M0 WebSocket 不退出。

### Definition of Done

- M1 FastAPI 服务由 systemd 托管并可重启恢复。
- 当前 ESP32 无需 OTA 即可完成一轮真实 STT → Agent → OLED。
- v0.0.103 OTA 文件、`despod.service` 的 M0 传输稳定性和 `despod-firmware.service` 均未被破坏。
- 测试、服务状态、配置路径和未完成风险已写入 HANDOFF。

## 10. 明确不做

- 不做 TTS 和扬声器；这是后续硬件/固件阶段。
- 不做多 ECS、高可用、Railway 迁移。
- 不做多设备压测。
- 不把 `/root/esp32-firmware` 当开发目录。
- 不因为“看起来更专业”而提前引入 Redis、Celery、MQTT、Kubernetes 或多 worker。
