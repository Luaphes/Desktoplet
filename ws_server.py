#!/usr/bin/env python3
"""
WebSocket 服务端（多 ESP32 支持）
监听 0.0.0.0:8765，支持多台 ESP32 同时在线。
命令文件支持 target 字段指定单台设备。
"""
import asyncio
import websockets
import json
import os
import sys
import time

ESP32s = {}
_next_id = 0
OTA_PORT = 23717
CMD_FILE = "/tmp/ws_cmd.json"
_last_btn_time = 0
_is_restored = False
_cmd_queue = []
_default_display_msg = ""
_waiting_loop = False

async def _safe_send(msg, target=None):
    """发送消息到指定或全部 ESP32。断连则入队。"""
    global ESP32s, _cmd_queue
    if target:
        ws = ESP32s.get(target)
        if not ws:
            _cmd_queue.append({"msg": msg, "target": target})
            return
        try:
            await ws.send(msg)
        except websockets.exceptions.ConnectionClosed:
            del ESP32s[target]
            _cmd_queue.append({"msg": msg, "target": target})
    else:
        for tid, ws in list(ESP32s.items()):
            try:
                await ws.send(msg)
            except websockets.exceptions.ConnectionClosed:
                del ESP32s[tid]
                _cmd_queue.append({"msg": msg, "target": tid})

async def _update_display(msg, target=None):
    if ESP32s:
        await _safe_send(msg, target)
        tgt = f" -> {target}" if target else " -> all"
        print(f"[CMD]{tgt} {msg}")
    else:
        _cmd_queue.append({"msg": msg, "target": target})
        print(f"[QUEUE] {msg}")

async def _animate_dots():
    global _waiting_loop
    dots = ["   ", ".  ", ".. ", "..."]
    i = 0
    while _waiting_loop and ESP32s:
        text = f"Hermes is waiting{dots[i % 4]}"
        await _safe_send(json.dumps({"type": "display", "text": text}))
        i += 1
        await asyncio.sleep(0.5)

async def _set_waiting():
    global _waiting_loop, _is_restored
    _waiting_loop = False
    await asyncio.sleep(0.1)
    _waiting_loop = True
    _is_restored = False
    asyncio.create_task(_animate_dots())

async def _restore_display():
    global _waiting_loop, _is_restored
    if _is_restored:
        return
    _waiting_loop = False
    await asyncio.sleep(0.1)
    boot = ["H", "HE", "HER", "HERM", "HERME", "HERMES"]
    for ch in boot:
        if not ESP32s:
            break
        await _safe_send(json.dumps({"type": "display", "text": ch}))
        await asyncio.sleep(0.25)
    _is_restored = True

async def _check_cmd_file():
    global _last_btn_time
    if not ESP32s:
        return
    try:
        with open(CMD_FILE) as f:
            msg = f.read().strip()
        if msg:
            try:
                cmd = json.loads(msg)
                if isinstance(cmd, dict):
                    target = cmd.pop("target", None)
                    if cmd.get("command") == "frames" and "frames" in cmd:
                        loop = cmd.get("loop", 1)
                        for _ in range(loop):
                            for frame in cmd["frames"]:
                                ft = frame.pop("target", target)
                                await _safe_send(json.dumps(frame), ft)
                                await asyncio.sleep(cmd.get("delay", 0.15))
                        os.remove(CMD_FILE); return
                    if cmd.get("command") == "waiting":
                        await _set_waiting(); os.remove(CMD_FILE); return
                    elif cmd.get("command") == "restore":
                        await _restore_display(); os.remove(CMD_FILE); return
            except json.JSONDecodeError:
                pass
            await _update_display(msg, target)
            os.remove(CMD_FILE)
    except (FileNotFoundError, OSError):
        pass

async def handler(websocket):
    global _next_id, _cmd_queue
    cid = str(_next_id)
    _next_id += 1
    ESP32s[cid] = websocket
    print(f"[+] ESP32 {cid} from {websocket.remote_address}")
    remaining = []
    for item in _cmd_queue:
        tgt = item.get("target")
        if tgt is None or tgt == cid:
            try:
                await websocket.send(item["msg"])
                print(f"[FLUSH] {item['msg']}")
            except:
                remaining.append(item)
        else:
            remaining.append(item)
    _cmd_queue = remaining
    try:
        async for message in websocket:
            data = json.loads(message)
            print(f"[ESP32:{cid}] {json.dumps(data, ensure_ascii=False)}")
            if data.get("type") == "btn_click":
                global _last_btn_time
                now = time.time()
                if now - _last_btn_time > 3:
                    _last_btn_time = now
                    await _restore_display()
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        ESP32s.pop(cid, None)
        print(f"[-] ESP32 {cid} disconnected")

async def stdin_forward():
    loop = asyncio.get_running_loop()
    while True:
        line = await loop.run_in_executor(None, input)
        if line:
            try:
                cmd = json.loads(line)
                if isinstance(cmd, dict) and cmd.get("command") == "frames":
                    for frame in cmd["frames"]:
                        await _safe_send(json.dumps(frame))
                        await asyncio.sleep(cmd.get("delay", 0.15))
                    print("[SENT] frames sequence"); continue
            except json.JSONDecodeError:
                pass
            if line == "list":
                print(f"Connected: {list(ESP32s.keys())}")
                continue
            await _update_display(line)
            if line.startswith("ota"):
                port = OTA_PORT if ' ' not in line else int(line.split()[1])
                subprocess.Popen(["python3", "-m", "http.server", str(port)])
                await _safe_send(json.dumps({"type": "ota", "url": f"http://118.31.46.156:{port}/firmware.bin"}))

def get_local_ip():
    import socket
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except:
        return "127.0.0.1"
    finally:
        s.close()

async def main():
    print("WebSocket 服务端启动，等待 ESP32 连接...")
    print(f"OTA 端口: {OTA_PORT}")
    async with websockets.serve(handler, "0.0.0.0", 8765, ping_interval=15, ping_timeout=10):
        try:
            if sys.stdin.isatty():
                await stdin_forward()
            else:
                while True:
                    await _check_cmd_file()
                    await asyncio.sleep(0.1)
        except (EOFError, OSError):
            while True:
                await asyncio.sleep(60)

if __name__ == "__main__":
    asyncio.run(main())
