# Mac 编译环境准备指南

## 你需要安装的

### 1. VS Code（如果没装）

从 code.visualstudio.com 下载安装。

### 2. PlatformIO 扩展

在 VS Code 扩展市场搜索 **PlatformIO IDE**，安装（作者 PlatformIO）。

安装完成后重启 VS Code，底部会出现一个蚂蚁头图标。

### 3. USB 串口驱动（CP2102）

ESP32-C3 SuperMini 用的串口芯片是 CP2102（或 CH340）。Mac 通常自带驱动，如果插上 USB 后 `ls /dev/cu.*` 没看到新设备:

```
brew install --cask silicon-labs-vcp-driver
```

## 编译和烧录步骤

### 第一步：从 ECS 拉固件源码

用 SFTP 把 `/root/esp32-firmware/` 目录整个拉到 Mac 本地。

```
目录结构：
esp32-firmware/
├── platformio.ini     ← 编译配置（板型、库依赖）
├── main.cpp           ← 主程序
├── wifi_manager.cpp/.h
├── websocket.cpp/.h
├── oled_display.cpp/.h
├── button.cpp/.h
├── mic_i2s.cpp/.h
└── ota.cpp/.h
```

### 第二步：在 VS Code 中打开

File → Open Folder → 选择刚刚拉下来的 esp32-firmware 目录。

### 第三步：编译

底部蓝色蚂蚁图标 → Project Tasks → Build。或 Ctrl+Shift+P → PlatformIO: Build。

第一次编译会下载 ESP32-C3 工具链和依赖库，约 200MB，等 1-3 分钟。

### 第四步：烧录

1. 用 USB-C 线连接 ESP32 到 Mac
2. 如果同时插着 TP4056 OUT+ 线，**先拔掉**（只让 USB-C 供电）
3. 底部蓝色蚂蚁图标 → Project Tasks → Upload
4. 如果日志卡在 "Connecting..." 不动：按住 ESP32 板子上的 BOOT 按钮 → 按一下 RST 按钮 → 松开 RST → 松开 BOOT
5. 看到 "Writing at..." 进度条说明在烧了
6. 烧完 ESP32 自动重启，OLDE 亮

### 第五步：验证

烧录完成后 OLED 上会显示 "Connecting..."，然后进入配网模式。

## 配网

第一次开机 ESP32 会开一个 WiFi 热点叫 "ESP32-Config"（或类似名字）。

1. 手机连这个热点
2. 浏览器打开 192.168.4.1
3. 填写：WiFi 名、WiFi 密码、ECS 地址（开发时填 Mac 局域网 IP:8765）
4. 保存 → ESP32 重启 → 连 WiFi → 连 WebSocket

## 后续更新全部走 OTA

以后改代码 → 在 Mac 上编译出新 .bin → 推到 ECS → ECS 推送给 ESP32 → 自动更新。

不需要再插 USB 线。

## 开发阶段用的 WebSocket 服务端

```bash
# Mac 终端
pip install websockets
python ws_server.py
```

服务监听 0.0.0.0:8765。ESP32 配网时填 Mac 的局域网 IP + 8765 端口。
