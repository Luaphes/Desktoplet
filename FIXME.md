# Desktoppy v19 固件待修清单

下次 git push 前逐项确认。

## P0（优先修）

- [ ] **中文居中修复** (`src/oled_display.cpp`)
  `getStrWidth` 对 UTF-8 返回 0→居中失效
  改: UTF-8 解码器逐字符 `u8g2_GetGlyphWidth` 累加真实宽度 ✅ 已修

- [ ] **服务端推帧动画** (`ws_server.py` + `src/main.cpp`)
  ESP32 存上次 bitmap 数据，加 `{"type":"move","x":N}` 改显示位置
  服务端只需要发偏移量（几字节），不重传整图
  实现左右晃、滚动、弹跳等动画，逻辑全在服务端

- [ ] **ws_server 断连崩溃** (`ws_server.py`)
  `_update_display` → `ESP32.send(msg)` 在 ESP32 断开时抛异常
  改: `_safe_send()` 包装，断开时入队不崩服务 ✅ 已修

- [ ] **多 ESP32 区分** (`ws_server.py` + `websocket.cpp`)
  改成多连接池 `ESP32s = {id: websocket}`，ESP32 连上先发 `{"type":"identify","id":"xxx"}`
  命令可指定目标，不指定则广播

- [ ] **配网 OLED 显示** (`src/wifi_manager.cpp`)
  `startConfigPortal()` 阻塞前已加 `showStatus("Config Mode", "ESP32-Config")` ✅

- [ ] **版本号自增** (`version.txt`)
  每次功能推送前 bump
  当前 v18 → 下次 v19

## P1

- [ ] **多 ESP32 场景下的识别** (`src/websocket.cpp` + `ws_server.py`)
  连接时发 `{"type":"identify","mac":"C8:F0:9E:XX:XX:XX"}`
  服务端用 MAC 区分设备，重启后不乱，可给每台命名

## 待验证

- [ ] OTA 全链路（GitHub Actions → Release → OTA 指令 → ESP32 下载更新）
- [ ] 初号机 OUT- 焊点修好后电源稳定性

## 流程

1. 改代码
2. 本地检查无语法错误
3. 用户说 **"推"** 才 git push
4. 等 GitHub Actions 编译完成
5. 用户拉 .bin 或 OTA 推送
