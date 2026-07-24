#!/usr/bin/env python3
"""
WebSocket 服务端（Mac 开发用）
用法: python3 ws_server.py
监听 0.0.0.0:8765，等待 ESP32 连接。

终端输入命令:
  显示: <文字>  → 推送到 OLED 显示
  状态: <文字>  → 显示状态行
  清屏          → 清空 OLED
  ota <端口>     → 在当前目录启动 HTTP 服务并推送 OTA
  JSON字符串    → 原样发送
"""

import asyncio
import websockets
import json
import subprocess
import socket
import os
import sys
import time

ESP32 = None
OTA_PORT = 23717
CMD_FILE = "/tmp/ws_cmd.json"
_last_cmd_mtime = 0
_last_display_msg = ""
_default_display_msg = ""

async def _update_display(msg):
    global _last_display_msg
    _last_display_msg = msg
    if ESP32:
        await ESP32.send(msg)
        print(f"[CMD] {msg}")

_waiting_loop = False
_last_btn_time = 0
_is_restored = False

async def _animate_dots():
    """等待动画：三个点轮流闪烁"""
    global _waiting_loop, ESP32
    dots = ["   ", ".  ", ".. ", "..."]
    i = 0
    while _waiting_loop and ESP32:
        try:
            text = f"Hermes is waiting{dots[i % 4]}"
            await ESP32.send(json.dumps({"type": "display", "text": text}))
            i += 1
            await asyncio.sleep(0.5)
        except Exception:
            break

async def _set_waiting():
    global _default_display_msg, _waiting_loop
    _waiting_loop = False  # 先停旧的
    await asyncio.sleep(0.1)
    _default_display_msg = _last_display_msg
    _waiting_loop = True
    _is_restored = False
    asyncio.create_task(_animate_dots())

async def _restore_display():
    global _waiting_loop, _is_restored
    if _is_restored:
        return  # 已在恢复态，不重复播
    _waiting_loop = False
    await asyncio.sleep(0.1)
    # 播放 HERMES 动画（大头版）
    boot = ["H", "HE", "HER", "HERM", "HERME", "HERMES"]
    for ch in boot:
        if not ESP32:
            break
        await ESP32.send(json.dumps({"type": "display", "text": ch}))
        await asyncio.sleep(0.25)
    # 停在 HERMES，不切回 Codex
    _default_display_msg = ""
    _is_restored = True

async def _restore_animation():
    """ESP32 重连时播放，仅当不在恢复态时"""
    global _is_restored
    if _is_restored:
        return
    boot = ["H", "HE", "HER", "HERM", "HERME", "HERMES"]
    for ch in boot:
        if not ESP32:
            break
        await ESP32.send(json.dumps({"type": "display", "text": ch}))
        await asyncio.sleep(0.2)
    await asyncio.sleep(2)

async def _check_cmd_file():
    global _last_cmd_mtime, ESP32
    if not ESP32:
        return
    try:
        mtime = os.path.getmtime(CMD_FILE)
        if mtime <= _last_cmd_mtime:
            return
        _last_cmd_mtime = mtime
        with open(CMD_FILE) as f:
            msg = f.read().strip()
        if msg:
            # 支持内部命令
            try:
                cmd = json.loads(msg)
                if isinstance(cmd, dict):
                    if cmd.get("command") == "waiting":
                        await _set_waiting()
                        os.remove(CMD_FILE)
                        return
                    elif cmd.get("command") == "restore":
                        await _restore_display()
                        os.remove(CMD_FILE)
                        return
            except json.JSONDecodeError:
                pass
            await _update_display(msg)
            os.remove(CMD_FILE)
    except (FileNotFoundError, OSError):
        pass

def get_local_ip():
    """获取 Mac 局域网 IP"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
    except Exception:
        ip = "127.0.0.1"
    finally:
        s.close()
    return ip

async def handler(websocket):
    global ESP32
    ESP32 = websocket
    print(f"[+] ESP32 connected from {websocket.remote_address}")

    # 重连时播 HERMES 动画
    await _restore_animation()

    try:
        async for message in websocket:
            data = json.loads(message)
            print(f"[ESP32] {json.dumps(data, ensure_ascii=False)}")
            # 收到按钮事件时恢复显示（带冷却）
            if data.get("type") == "btn_click":
                now = time.time()
                if now - _last_btn_time > 3:
                    _last_btn_time = now
                    await _restore_display()
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        ESP32 = None
        print("[-] ESP32 disconnected")

async def stdin_forward():
    """从终端输入消息推给 ESP32"""
    loop = asyncio.get_event_loop()
    http_proc = None

    while True:
        line = await loop.run_in_executor(None, input)
        if not line:
            continue

        if line.startswith("ota"):
            # 启动 HTTP 服务提供 .bin 下载
            if http_proc:
                http_proc.terminate()
                print("[HTTP] 旧服务已停止")

            http_proc = subprocess.Popen(
                ["python3", "-m", "http.server", str(OTA_PORT)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            ip = get_local_ip()
            url = f"http://{ip}:{OTA_PORT}/firmware.bin"
            msg = json.dumps({"type": "ota", "url": url})
            print(f"[OTA] HTTP 服务已启动: {url}")

            if ESP32:
                try:
                    await ESP32.send(msg)
                    print(f"[SENT] {msg}")
                except Exception as e:
                    print(f"[ERR] {e}")
            continue

        if line.startswith("显示:"):
            text = line[3:].strip()
            msg = json.dumps({"type": "display", "text": text}, ensure_ascii=False)
        elif line.startswith("状态:"):
            text = line[3:].strip()
            msg = json.dumps({"type": "status", "line1": text}, ensure_ascii=False)
        elif line.startswith("清屏"):
            msg = json.dumps({"type": "clear"})
        else:
            msg = line

        try:
            if ESP32:
                await ESP32.send(msg)
                print(f"[SENT] {msg}")
            else:
                print("[!] ESP32 未连接")
        except Exception as e:
            print(f"[ERR] {e}")

async def main():
    print("WebSocket 服务端启动，等待 ESP32 连接...")
    print(f"OTA 端口: {OTA_PORT}")
    print()

    async with websockets.serve(handler, "0.0.0.0", 8765):
        # 有终端交互时用 stdin_forward，后台模式则只等待
        try:
            if sys.stdin.isatty():
                await stdin_forward()
            else:
                # 后台模式：监控命令文件
                while True:
                    await _check_cmd_file()
                    await asyncio.sleep(2)
        except (EOFError, OSError):
            # 无终端时直接挂起等待
            while True:
                await asyncio.sleep(60)

if __name__ == "__main__":
    asyncio.run(main())
