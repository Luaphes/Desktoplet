# Handoff — OTA 鲁棒性 + 系统稳定性治理 (v96)

## 本轮完成

### OTA 架构升级 (v87 → v96)

| 版本 | 改动 | 效果 |
|------|------|------|
| v87 | 版本角标 + 四角轮跳 | OLED 右下角常驻固件版本 |
| v87 | 按住 BTN 3s WiFi 重置 | 配网入口 |
| v88 | DMA buffer 4→6 | 减少音频丢帧（仍有 95% 丢失） |
| v88 | 按键录音 OLED 音量条 | "REC" + 进度条显示 |
| v89 | OTA 进度条 (HTTPUpdate.onProgress) | 下载时显示百分比 |
| v90 | WiFi OK + IP 居中显示 | UI 优化 |
| v91 | OTA 延迟到 loop() | 不在 WS 回调里阻塞，解决 OTA 超时 |
| v92 | 松手恢复 WiFi OK 画面 | 不再卡在 REC 画面 |
| v93 | WiFiManager 内部存储 | 移除 Preferences 依赖 |
| v94 | 音量阈值 8192→256 | INMP441 低电平适配 |
| v95 | WiFiClient → WiFiClientSecure | ESP32 HTTPS 直连 GitHub CDN |
| v96 | esp_ota_mark_app_valid_cancel_rollback() | 新固件崩了自动回退旧版本 |

### 后端稳定性

- ws_server → **systemd 托管** (`despod.service`)
  - 崩溃自动重启（6 秒恢复）
  - 日志走 journald，实时不缓冲
  - `systemctl status despod` 随时查状态

### 文档

- `ARCHITECTURE.md` — v1→v4 架构演进路径
- `FIXME.md` — OTA 全链路标记为 ✅

---

## 当前状态

```
板子: ESP32-C3 SuperMini
固件: v93（卡在线上，v96 推了但 OTA 刷不上去）
服务: ✅ systemd 稳定运行 1 天+
```

## 阻塞问题

**ESP32 反复崩溃重启**（所有 v87+ 版本共有）：

```
连接 30-80s → 崩溃 → 重启 → WiFiManager 磨蹭几分钟 → 重连
```

OTA 下载需要 38s+（ghproxy→ECS→ESP32），刚好掉在崩溃窗口里 → OTA 永远刷不上去。

疑因：单核 C3 的内存碎片 / I2S DMA 冲突 / 看门狗
定位：需要串口日志

## 下一轮要做

1. **排查崩溃根因** — 接串口看 panichandler 输出
2. **OTA v96 上去** — 一旦 ESP32 稳定，立刻 OTA
3. **验证回退** — 故意刷坏固件，确认能自动回退
4. **音量问题** — INMP441 +30dB 才能听清

## 关键数据

```
固件仓库: Luaphes/Desktoppy (main)
分区表: partitions_ota.csv (app0 1.75MB + app1 1.75MB + otadata)
OTA 架构: ESP32 WiFiClientSecure → GitHub CDN 直连（不走 ECS）
WS 端口: 118.31.46.156:8765 (despod systemd)
HTTP: 118.31.46.156:23717 (python http.server)
音频: INMP441 16kHz 16bit mono PCM, /tmp/esp32_audio_*.raw
```
