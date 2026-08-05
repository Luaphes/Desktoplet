# Handoff — v0.0.103 M0 稳定基线与 ECS 接管

## 当前状态

- 硬件：ESP32-C3 SuperMini，设备 MAC `14:63:93:90:CF:94`
- 固件：v0.0.103（M0 本地录音/后台上传链路已通过）
- Git 基线：`main@0dc42b2`，发布标签 `v0.0.103`
- ECS WebSocket：`118.31.46.156:8765`，由 `despod.service` 托管
- ECS OTA：`http://118.31.46.156:23717/firmware.bin`，由 `despod-firmware.service` 托管

## ECS 接管状态（2026-08-01）

- 开发工作区：`/root/Desktoppy`，应从 GitHub `main` 继续开发。
- 线上运行目录：`/root/esp32-firmware`。该目录承载 systemd 服务和发布固件，不作为日常开发工作区。
- 已发布固件：`/root/esp32-firmware/releases/v0.0.103/firmware.bin`。
- OTA 固件 SHA-256：`c9087229238a1cfe8684bc15bd77c4f39fe2dc75e1baf9f34d4e02f3d5be3dbf`。
- 回滚副本：`/root/esp32-firmware/firmware.previous.bin`。
- OTA 命令队列当前为空；板子最近回连由 ECS 上报 `version=103`，v0.0.103 新版本标识尚未在设备日志中确认。
- ECS 当前未安装 PlatformIO。下一位 Agent 若要完全在 ECS 开发和发布，应先建立可重复的构建环境，再继续修改固件；不要重新依赖本地串口刷写。

## M0 OTA 基线（冻结）

- `v0.0.103` 是当前线上 M0 稳定基线；除 OTA 回滚或安全修复外，不再从 M1 分支向板子发布固件。
- 只有 `main` 的版本化发布才允许进入 OTA 服务；`m1-stt-pipeline` 只做 artifact 和验证，不自动 release、不自动 OTA；不会进入 OTA 发布路径。
- OTA 完备性已按“远程下载、双分区、失败重试、60 秒在线确认、ECS 版本上报”验收；后续 M1 验证不能破坏这条路径。
- 当前开发接力分支：`m1-stt-pipeline`；M1 验证提交已归入该功能分支，尚未进入 `main`。

## 本轮根因与修复

此前“30–80 秒后崩溃”的主要现象不是 CPU panic。持续启动 I2S DMA 会拖垮 ESP32-C3 的 Wi-Fi / WebSocket；关闭空闲 I2S 后，设备能持续在线。

本轮已完成：

1. I2S 只在录音或测试时启动，结束后停止。
2. 录音缓冲增至 1024 字节，OLED 音量条限制为最高 10 Hz，避免 I2C 刷屏阻塞音频上传。
3. 单次录音上限 30 秒；超时、断网或 OTA 都会停止 I2S。
4. 产品按键从 GPIO0 迁至 GPIO1，避开 ESP32-C3 启动绑带脚。
5. Wi-Fi 在 WPA 握手前关闭省电并提高发射功率；配网门户持续开放，断线自动重连。
6. WebSocket 超过 30 秒未恢复时重置 STA，并增加 10 秒健康日志。
7. OTA 支持 HTTP / HTTPS、失败最多重试 3 次、下载完成后显式干净重启。
8. 新固件在 Wi-Fi 与 WebSocket 连续在线 60 秒后才请求标记有效。
9. WebSocket 连接后上报 MAC 与固件版本，ECS 可确认 OTA 的真实目标版本。
10. ECS 音频文件在一次连接内保持打开，减少每个音频包重复打开文件的开销。
11. I2S 停止时完整卸载驱动并释放 DMA；Wi-Fi 重连间隔改为 30 秒，避免反复取消尚未完成的 WPA 握手。
12. INMP441 按 32-bit I2S 时隙读取，再转换为带增益的 16-bit PCM，修正伪满量程音量。
13. 丢弃首个 I2S 启动瞬态，扩大 DMA 环并将音频上行聚合为 4 KB WebSocket 包。
14. 小块持续读取 I2S、内存聚合为 8 KB WebSocket 帧，并使用 512 ms DMA 环吸收公网发送等待。
15. 音频上行改为 16 kHz G.711 mu-law（16 KB/s、1 KB 帧），ECS 实时解码回 16-bit PCM；避免大 WebSocket 帧长时间阻塞按钮与 UI。
16. 录音与公网彻底解耦：最多 15 秒 mu-law 先写 SPIFFS，松手立即停 I2S/恢复 UI，再按 1 KB 帧后台上传。

## 已验证

- 本地构建 v98 成功。
- v97 从 ECS 下载固件，自行重启并回连；ECS 收到同一 MAC 的 `version=98`，证明远程 OTA 闭环成功。
- OTA 固件本地与 ECS SHA-256 一致。
- ECS 两个 systemd 服务均为 `active`。
- 服务端使用模拟 WebSocket 音频流接收 64 × 1024 字节，落盘大小准确为 65536 字节，服务未退出。
- 带空格 JSON：`{"type": "display", "text": "OTA OK"}` 已被设备确认处理。
- v98 实机录音约 5.8 秒，ECS 收到 90,112 字节和完整 start/stop；随后复现出 I2S 停止后网络失效，已作为 v99 修复目标。
- v99 实机录音后网络保持在线，确认 I2S/DMA 释放修复有效；音频小包吞吐不足作为 v100 修复目标。
- v100 实机 9.1 秒发送 114,688 字节，网络继续稳定；吞吐仍只有目标的约 39%，作为 v101 聚合发送修复目标。
- v101 的 8 KB 同步帧导致一次写入长时间阻塞，按钮松开约 26.8 秒后才处理，方案已废弃并由 v102 mu-law 小帧替代。
- v102 已由 v101 远程 OTA 成功，同一 MAC 上报 `version=102`，连续在线 60 秒后标记有效。
- ECS mu-law 解码使用 4,096 字节合成输入验证，输出为准确的 8,192 字节 16-bit PCM。
- v102 实机仍出现控制 WebSocket 长阻塞，第二次 `mic_start` 未到 ECS；因此不再边录边传，改为 v103 store-and-forward。
- v103 已由 v102 远程 OTA 成功，SPIFFS 首次格式化后容量 293,921 字节，同一 MAC 上报 `version=103` 并连续在线 60 秒后标记有效；合并主线时将版本格式规范为 `v0.0.103`。
- v0.0.103 对应功能实机录得 203,573 字节 mu-law，ECS 解码为准确的 407,146 字节 PCM（12.72 秒，约为理论吞吐的 99.5%）；上传约 6.2 秒完成，设备保持在线。
- v0.0.103 对应功能短录音回归：按钮松开后 27 ms 完成停止，本地 60,353 字节、ECS PCM 120,706 字节，上传约 2.42 秒，随后 20/20 ping 成功且 WebSocket 未断开。M0 功能验收通过；OLED 音量条流畅度作为非阻塞 UI 优化保留。

## 后续回归建议

1. 长时间运行后再次抽查空闲 ping 与 ECS 断连记录。
2. 后续修改音频或 OLED 时，复测本地字节数、ECS PCM 2 倍关系、松手响应和上传后在线状态。
3. OLED 音量条流畅度可以单独优化，但不要重新引入录音期间的公网同步发送。

不要再用 GPIO0 做按钮测试。GPIO0 接地并重启可能进入下载模式。

## 下一阶段

M1 的最小闭环是：`recording_id` → 本地录音上传 → STT → `job_id` → Agent job → `progress / result / error` → OLED。当前服务端仍只保存原始 PCM，尚未接入 STT、Agent、Chatbox 或 TTS。

下一位 ECS Agent 的首要验收顺序：

1. 等设备上线，确认日志依次出现 OTA 指令、`GET /firmware.bin`、断线重启和 `version=v0.0.103`。
2. 上线后持续观察至少 60 秒，并确认 `despod.service`、`despod-firmware.service` 仍为 `active`。
3. 再开始 M1 协议设计；不要破坏 v0.0.103 已验证的 store-and-forward、GPIO1、I2S 完整卸载和 OTA 回滚路径。

## M1 验证记录（2026-08-06）

- `m1-stt-pipeline` 作为唯一 M1 功能分支；CI 版本归一化、YAML 解析、mu-law/WAV、mock STT→Agnes→OLED 多行消息均通过。
- 修复了服务端结果中的字面量 `\\n` 缺陷，现会发送真正的换行字符，C3 多行 OLED 逻辑才能生效。
- 使用 ECS 历史 v103 PCM 样本完成真实 STT→Agnes→OLED 验证：120,706 字节、约 3.77 秒，三段链路均返回成功。
- 当前线上仍是 v103 M0 和旧版 `despod.service`；该功能分支当前不部署、不 release、不 OTA。实时按键采集仍需现场回归。
