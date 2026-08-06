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


## M1 后端实现接力（2026-08-06）

- 实现入口：`/root/Desktoppy/M1_BACKEND_IMPLEMENTATION.md`。
- canonical 分支：`m1-stt-pipeline`，当前设计提交：`42b3593`。
- ECS Agent 只在 `/root/Desktoppy` 开发；`/root/esp32-firmware` 继续作为线上 M0 运行目录，禁止直接替换。
- 第一阶段确认使用 FastAPI + 单进程 Uvicorn + systemd + SQLite 元数据 + 有界 asyncio 队列；M1 只绑定 `127.0.0.1:8786`。
- 目标是当前 MAC `14:63:93:90:CF:94` 的无 OTA canary：现有 v0.0.103 WebSocket 接收录音，M1 顺序执行 STT → Agent → display → OLED。
- 本阶段不做 TTS、多 ECS、Railway 迁移和多设备压测；M0 WebSocket、OTA 服务和固件基线保持不变。
- ECS Agent 开始编码前必须先读完整实现说明；每完成一小段先跑文档规定的测试，再更新本 HANDOFF 的状态和风险。

## M1 后端实现状态（2026-08-06 本轮）

- **代码落地**（均在 `/root/Desktoppy`，分支 `m1-stt-pipeline`）：
  - `m1_service/`：config（EnvironmentFile 读 key）/ models / store（SQLite jobs 表）/ worker（Queue(maxsize=1)+in-flight 计数、状态机、重启恢复）/ app（healthz + POST/GET jobs）/ providers/stt.py（SiliconFlow SenseVoiceSmall）/ providers/agent.py（Agnes 2.5 Flash）
  - `ws_server.py` 改造：删除 b8cb47a 内嵌 M1 实验代码（不再直接持有 key），改为 canary 路由——identify 登记 MAC，仅目标 MAC `14:63:93:90:CF:94` 在 mic_stop 后 POST `127.0.0.1:8786/internal/v1/jobs` 并轮询下发 OLED 事件（处理中.../没听清.../你说：…+回复摘要/服务暂不可用，4 行截断）；不打印 transcript
  - `ops/despod-m1.service`（草案，未安装）+ `ops/despod-m1.env.example`（占位符模板）
  - `tests/`：test_m1_service.py（11 用例）、test_ws_canary.py（4 用例）、test_m1_sample.py（真实 API，无 key 时 skip）
- **验收 A（不接板子）结果**：
  - 单元 + canary 集成 15/15 全绿（健康检查/幂等/409/路径穿越/失败状态/重启恢复/OLED 截断/M1 不可达降级）
  - 真实 uvicorn 127.0.0.1:8786 验证：healthz 200、POST 202、重复提交 200 同 job、穿越 400
  - 历史 v103 样本 `/tmp/esp32_audio_2_1785524428_2.raw`（120,706B）真实链路：STT→"1秒2秒3秒。"→Agnes 回复，job done
- **剩余风险 / 待办**：
  1. `despod-m1.service` 未安装（systemd 托管留到 Canary 验收阶段）
  2. ws_server.py 的 WS→canary 完整路径需真实板子回归（验收 B）；本机 8765 被线上占用无法本地端到端
  3. M1 key 注入依赖 /etc/despod-m1.env（安装服务时创建，chmod 600）
  4. 代码审查：自查完成（2026-08-06），独立复核 agent 结论待追加

## 独立审查结论（deleg_d8f938f5，2026-08-06）

- 结论：🟡 有条件通过 Canary 验收；独立审查确认全部安全/契约硬约束满足，
  b8cb47a 内嵌代码彻底清除、测试全绿、无 Redis 等越界组件
- 审查发现并已修复：
  1. 【真实 bug】队列满 409 时 job 已写 SQLite 但卡在 received 永不处理
     （重启才会恢复重试）→ submit 失败时显式置 failed/error_code=queue_full
  2. 【契约】409 后 ws_server 显示「处理中...」但本次录音实际未被接受
     → 改为「服务忙，稍后再试」诚实提示
  3. 【契约】healthz 用查询不存在 job 验证 DB 不诚实 → 改 store.health()
     显式 SELECT 1，失败返回 503
  4. 【防御】set_status 无状态合法性校验 → 加 _VALID_STATUSES 检查
  5. 测试补断言：409 后 DB job 为 failed/queue_full
- 审查提出但评估不修的：
  1. CANARY_MAC 硬编码（单设备阶段契约即写死该 MAC）
  2. venv 绝对路径（文档 §8 允许确认依赖后使用虚拟环境）
  3. 运行时状态机流转校验（worker 逻辑已保证单向；set_status 已有合法值校验）
- 独立复核 agent 2（deleg_134bff3f）结论待合并

## 独立复核结论（deleg_134bff3f，2026-08-06）

- 结论：修复 2 项后可进入 Canary；其余 P2 不阻塞
- 采纳并已修复：
  1. 幂等重复提交返回 200 → 统一 202（契约字面「成功创建返回 202」），
     M0 侧本就接受 200/202 无行为差异；测试断言同步
  2. poll 请求 timeout 2s → 5s（M1 偶发慢响应不误判；submit 保持 2s 快速失败）
  3. 补 3 个测试：poll 404 降级、poll deadline 超时降级（fake http.server）、
     处理中 job 幂等（不重复调用）
- 误报项（代码已正确，复核读取窗口不全）：
  1. 「空 transcript 未传 transcript=``」——worker.py:113 已传，测试已断言
  2. 「429 不重试」——循环结构保证 429 进入下一轮 attempt（指数退避）
  3. 「healthz sentinel key」——此前已改 store.health() SELECT 1（复核基于旧快照）
- 评估不修：ws_server print 风格（与 M0 全文件一致，不重构）；audio_root
  默认 /tmp 已与 M0 落盘一致（config docstring 说明）；运行状态机流转校验
  （set_status 已有合法值校验）

## 最终审查状态（2026-08-06）

- 两轮独立审查 + 自查全部闭环；验收 A 全部通过
- 当前测试：20 用例（18 pass + 2 skip 需真实 key）；真实样本链路已验证
- 可进入 Canary 验收（B 阶段）：安装 despod-m1.service + 真实板子回归

## 代码审查结论（自查，2026-08-06）

- **严重（契约/安全）**：无
- **审查中发现并已修复**：
  1. Queue(maxsize=1) 只限制排队中任务，实际并发上限为 2 → 增加 in-flight 计数，队列满严格 409（worker.py）
  2. uvicorn 缺少模块级 app 入口 → 补 `app = create_app()`（app.py）
  3. recording_id fallback 缺设备标识，多设备同秒录音会撞号 → fallback 加 cid（ws_server.py）
  4. 幂等检查在路径校验之后，重复提交遇非法路径会 400 而非返回原 job → 幂等提前（app.py，契约「重复提交返回原 job」）
  5. `resp.json()` 解析异常未捕获会导致后台任务静默失败 → 捕获 ValueError/KeyError 并降级 OLED 提示（ws_server.py）
  6. providers 函数内 `import asyncio` → 移至模块顶部（风格）
- **已知边界（记录不修）**：
  1. Agent 上下文延续未实现：session_id/device_id 已传参，但 Agnes 调用为单轮无状态——契约要求「M1 Orchestrator 不自行发明记忆系统」，会话上下文属 Agent Gateway 职责，本阶段符合契约
  2. `get_public_ip()` 为 b8cb47a 遗留的 OTA 相关改动，不在 M1 范畴，保留
  3. 恢复的 job 若在 Agent 阶段崩溃，重启后会重新执行 STT（重复调用一次模型）——契约接受「扫描恢复避免丢任务」，幂等在 recording_id 层保证不重复创建
- **准入建议**：验收 A 全部通过，可进入 Canary（B 阶段）


## M1 Canary 实时状态更新（2026-08-06 23:20 CST）

> 本节覆盖前文“服务尚未安装 / 等待真实板回归”的旧状态，接力时以本节为准。

- despod-m1.service 已安装并由 systemd active 托管，监听 127.0.0.1:8786；despod.service 与 despod-firmware.service 同时 active。
- 当前板子已真实回连，MAC 14:63:93:90:CF:94，固件 v0.0.104；本次 M1 修复不需要 OTA。
- 真实板端最近一次录音已落盘并成功提交 M1；此前唯一失败 job job-8b7cba74ea47 的根因为 agent_timeout，不是收音、STT 或 WebSocket 断线。
- 以同一板端音频 /tmp/esp32_audio_4_1785988842_1.raw 做真实后端重放：约 17.9 秒完成 STT → Agent，status=done，transcript/reply 均非空。
- 本轮修复：
  1. Agent 默认 timeout 15s → 30s，有限重试 2 → 1，保留有界等待；
  2. M1 job/poll 预算扩到 180s，避免后端仍在处理时 Gateway 先发“服务暂不可用”；
  3. display JSON 使用 ensure_ascii=False，中文以 UTF-8 原样发给固件；
  4. OLED 成功态只显示 Agent 回复摘要（最多 4 行），处理中/失败/没听清仍有明确状态。
- 验证：unittest 21 项通过，真实 API 诊断调用 3/3 成功；重启后健康检查 200，三项 systemd 服务 active。
- 当前唯一未完成项：用户在板端再说一句短句，确认真实 WebSocket 回传的“处理中 → Agent 回复”确实落到 OLED。若仍无回显，优先看 journalctl -u despod.service 的 job ... done 和 display 下发日志，不再重新刷固件。



## v0.0.105 OLED UTF-8 修复（2026-08-06 23:30 CST）

- 真实板 v0.0.104 的 job 已完成：STT transcript 与 Agent reply 均非空；OLED 出现两个异常符号，问题不在 ECS。
- 根因：M1 中文 display 路径使用 u8g2.drawStr()，该 API 将 UTF-8 多字节序列当作单字节字形；WQY 中文字体必须使用 u8g2.drawUTF8()。
- 修复提交：9689ba9，version.txt 升为 v0.0.105，本机 PlatformIO 隔离构建成功（RAM 14.3%，Flash 62.1%）。
- 当前状态：M1 分支已推送；main 是其祖先，可安全 fast-forward 发布。发布/OTA 前仍需保留 v0.0.104 回滚可用性。



## v0.0.105 OTA 结果（2026-08-06 23:40 CST）

- GitHub Release HTTPS 直链在 ESP32 上出现 OTA Failed，原因是 release URL 的 302/CDN 下载链路不适配当前设备 OTA 客户端；板子未掉出 v104。
- 已将正式 GitHub Release artifact（SHA-256 51c9555a5895c09014dd282d113c10329ad18d385fa1f8e6d845dce04d1055a7）放入 ECS OTA 目录；旧 v104 备份为 firmware.v104.before-v105.bin。
- 通过 ECS HTTP http://118.31.46.156:23717/firmware.bin 重试成功：HTTP 200，设备断线后于 23:39:18 上报 v0.0.105，M1 healthz 仍为 200。
- 剩余验收：在 v105 上重新进行一次真实语音，确认 Agent 中文回复不再出现异常符号；确认后冻结 v105 OLED 修复。



## M1 OLED 回复换行修复（2026-08-06 23:47 CST）

- v0.0.105 已能正确绘制 UTF-8，但首次真实回复只显示“我是Agnes，由Sa”，原因是 ws_server 原先对单行 reply 只取前 12 字符。
- 提交 c022a0d：服务端按每行最多 12 字符自动换行，最多 4 行，超出最后一行才加省略号；固件不变，不需要 OTA。
- 22 项 unittest 全绿（2 项真实密钥样本 skip）；线上 despod.service 已重启，v0.0.105 板子重新回连。
- 下一步：再做一次真实语音，确认完整短回复能显示 2-4 行；随后冻结 v0.0.105。



## OLED 专用 Agent 提示词（2026-08-06 23:55 CST）

- 提示词已从“50 字/4 行”收紧为：只输出最终答复、不要自我介绍/Markdown/emoji/引号、优先结论、约 36 个中文等宽字符、尽量不超过 3 行。
- 提交 27a3aba；M1 已重启，真实 API 验证返回 25 字符/1 行。
- 提示词只是软约束，硬限制仍由 ws_server 的 4 行自动换行与省略号保证。
- 自动滚动暂不进入 v105：当前板端无分页协议，先稳定固定 4 行；后续可做 Gateway 定时分页或 v106 固件滚动。


## v0.0.106 OLED 中文字库覆盖修复（2026-08-07）

- 现象：回复中的“获取”显示成“取”，例如“我无法取你当前的位置”；传输和 Agent 文本本身正常。
- 字形探针确认：`u8g2_font_wqy12_t_chinese2` 仅含 574 个字，U+83B7“获”不存在；同一探针在 `wqy12_t_gb2312` 中命中。
- 修复提交：f644a88；display 路径改用 `u8g2_font_wqy12_t_gb2312`，保留 `drawUTF8`，版本升为 v0.0.106。
- 隔离 PlatformIO 构建通过：RAM 14.3%，Flash 72.4%（1,329,098 / 1,835,008 bytes），仍在 OTA 分区内。
- GitHub Actions run 31118543206 已触发，当前等待 runner；ECS OTA 目录暂保留 v105，未用本机临时包绕过 release。
- 发布后验收：先通过 `http://118.31.46.156:23717/firmware.bin` OTA；再下发/触发“我无法获取你当前的位置”样本，确认“获”与其它中文均可见。


## M1 延迟与 Hermes Gateway 接力（2026-08-07）

### CI 真实状态

- run `31118543206` 不是“仍排队”：最终结论为 `failure`。
- build job `92673920330` 结论为 `cancelled`，GitHub 注释为：Hosted Runner 多次尝试后仍未获取到 runner。
- 这是 GitHub runner 基础设施失败，不是 v0.0.106 源码编译错误；本机隔离 PlatformIO 构建已通过，Flash 72.4%。
- 没有生成正式 Release artifact；ECS OTA 目录保持 v105。后续先重跑 Hosted Runner，再做 v106 OTA。

### 当前真实链路

```text
ESP32 录音
  -> ESP 本地 SPIFFS 录音文件
  -> M0 WebSocket :8765（上传 mulaw 分片，收到 mic_stop）
  -> M1 POST 127.0.0.1:8786
  -> SiliconFlow STT
  -> Agnes OpenAI-compatible /v1/chat/completions
  -> M1 job done
  -> M0 轮询后发送 display JSON
  -> OLED
```

- `ws_server.py` 不直接调用 STT/Agent；`m1_service/worker.py` 顺序调用 `stt_provider.transcribe` 与 `agent_provider.chat`。
- `m1_service/providers/agent.py` 当前是 Agnes 的薄 HTTP adapter：直接 POST JSON `messages=[system,user]`。
- 当前 `session_id` / `device_id` 只传入函数签名，没有拼入 Agent 历史；当前不是 Hermes Session/Memory 闭环。
- M0 的 `_fit_oled` 只负责换行/截断；JSON 不是慢点。当前 Agent 非流式，必须完整返回后才下发 OLED。

### 延迟证据（ECS 日志/SQLite）

- ESP 上传段：M0 的 `mic_start -> mic_stop` 即录音文件上传窗口；近期样本约 `3.47s`（126,848 bytes）、`4.26s`（145,264 bytes）、`7.19s`（171,858 bytes）。固件每轮主循环只发送一个 1KB WebSocket binary frame。
- M0 -> M1：`mic_stop -> job submitted` 约 `45-67ms`，不是秒级瓶颈。
- M1 provider 段：近期 job 从 created 到 done 约 `1.6s`、`2.0-2.6s`、`32.4s`；现有 store 只保留最终 `updated_at`，还不能区分 STT 与 Agent 各自耗时。
- 轮询间隔是 `0.5s`，OLED JSON/绘制耗时可忽略。体感“拔线后迟迟没有处理中”主要来自 ESP 上传；“处理中后长时间不变”来自 STT+Agent 串行、非流式及其公网长尾。
- STT 默认 timeout 30s、最多 2 次重试；Agent timeout 30s、最多 1 次重试，失败重试会进一步放大长尾。不要仅靠降低 timeout 修复。

### Hermes 现状与正确入口

- ECS 当前 `hermes-gateway.service` 是 `gateway run --profile feishu`，用于 Feishu 消息接入；它不是设备 API，当前没有监听 8642/9119。
- Hermes 已安装内置 `gateway/platforms/api_server.py`，提供 OpenAI-compatible `POST /v1/chat/completions`、`X-Hermes-Session-Id` 会话续接、SQLite session store 和 SSE streaming；当前未启用。
- `hermes serve` 是桌面/远程客户端用的 JSON-RPC/WebSocket backend，不能等同于设备 Agent Gateway；不要让 ESP 直接连 Feishu messaging gateway。
