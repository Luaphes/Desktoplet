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
import httpx

# ---- M1 canary：只对目标 MAC 路由到 M1 Orchestrator (127.0.0.1:8786) ----
# M1 是独立 FastAPI 服务；本文件不直接调用 STT/Agent API。
CANARY_MAC = "14:63:93:90:CF:94"
M1_BASE = "http://127.0.0.1:8786"
M1_POLL_INTERVAL = 0.5
# Provider 端允许有限重试；轮询窗口要覆盖 STT/Agent 的正常长尾，
# 避免后端仍在处理时 gateway 先给 OLED 发“服务暂不可用”。
M1_POLL_TIMEOUT = 180.0

ESP32s = {}
_next_id = 0
OTA_PORT = 23717
CMD_FILE = "/tmp/ws_cmd.json"
_last_btn_time = 0
_is_restored = False
_cmd_queue = []
_default_display_msg = ""
_waiting_loop = False
_device_macs = {}  # cid -> MAC（identify 时登记）


def _mulaw_to_pcm16(data):
    """Decode G.711 mu-law bytes to little-endian signed 16-bit PCM."""
    out = bytearray(len(data) * 2)
    for i, encoded in enumerate(data):
        value = (~encoded) & 0xFF
        sign = value & 0x80
        exponent = (value >> 4) & 0x07
        mantissa = value & 0x0F
        sample = (((mantissa << 3) + 0x84) << exponent) - 0x84
        if sign:
            sample = -sample
        sample &= 0xFFFF
        out[2 * i] = sample & 0xFF
        out[2 * i + 1] = sample >> 8
    return out

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
    audio_file = None
    audio = None
    audio_codec = "pcm16"
    recording_number = 0
    try:
        async for message in websocket:
            if isinstance(message, bytes):
                if not audio:
                    print(f"[AUDIO] ESP32 {cid} ignored {len(message)} bytes without mic_start")
                    continue
                if audio_codec == "mulaw":
                    audio.write(_mulaw_to_pcm16(message))
                else:
                    audio.write(message)
            else:
                data = json.loads(message)
                print(f"[ESP32:{cid}] {json.dumps(data, ensure_ascii=False)}")
                if data.get("type") == "identify":
                    _device_macs[cid] = str(data.get("mac", "")).upper()
                    print(f"[IDENTIFY] ESP32 {cid} mac={_device_macs[cid]} "
                          f"version={data.get('version', '?')}")
                elif data.get("type") == "btn_click":
                    global _last_btn_time
                    now = time.time()
                    if now - _last_btn_time > 3:
                        _last_btn_time = now
                        await _restore_display()
                elif data.get("type") == "mic_start":
                    if audio:
                        audio.close()
                    recording_number += 1
                    audio_codec = data.get("codec", "pcm16")
                    audio_file = (f"/tmp/esp32_audio_{cid}_{int(time.time())}_"
                                  f"{recording_number}.raw")
                    audio = open(audio_file, "ab", buffering=0)
                    print(f"[AUDIO] ESP32 {cid} streaming {audio_codec} to {audio_file}")
                elif data.get("type") == "mic_stop":
                    if audio:
                        audio.close()
                        audio = None
                    size = (os.path.getsize(audio_file)
                            if audio_file and os.path.exists(audio_file) else 0)
                    print(f"[AUDIO] ESP32 {cid} stopped, {size} bytes saved to {audio_file}")
                    # M1 canary：目标 MAC 录音结束后路由到 M1 Orchestrator。
                    # 其它设备/未识别 MAC 保持 M0 原行为。
                    mac = _device_macs.get(cid, "")
                    if size > 0 and audio_file and mac == CANARY_MAC:
                        # 固件 mic_stop 不带 id；fallback 含 cid 防止多设备撞号
                        rid = data.get("id", f"rec_{cid}_{int(time.time())}_{recording_number}")
                        asyncio.create_task(_submit_m1_job(rid, audio_file, cid, mac))
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        if audio:
            audio.close()
        ESP32s.pop(cid, None)
        _device_macs.pop(cid, None)
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
                await _safe_send(json.dumps({"type": "ota", "url": f"http://{get_public_ip()}:{port}/firmware.bin"}))

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

def get_public_ip():
    import urllib.request
    try:
        return urllib.request.urlopen("http://ifconfig.me", timeout=2).read().decode().strip()
    except:
        return "118.31.46.156"  # fallback to known static IP

# ---- M1 canary：提交任务 + 轮询状态 + OLED 事件 ----

async def _display(text: str, cid: str):
    """发送 OLED display 事件。

    ensure_ascii=False 让中文/换行以 UTF-8 原样进 JSON——固件 JSON 解析
    不处理 \\uXXXX 转义（此前 OLED 显示过字面 \\uFF0C 乱码），
    \\n 换行是固件支持的标准转义（多行 OLED 已验证）。
    """
    await _safe_send(json.dumps({"type": "display", "text": text},
                                ensure_ascii=False), cid)


def _fit_oled(text: str, max_lines: int = 4, max_chars: int = 12) -> str:
    """OLED 128x64 / wqy12 中文约 10-12 字一行、最多约 4-5 行。
    服务端先截断，避免大段文本推给板子。"""
    lines = text.split("\n")
    out = []
    for ln in lines:
        if len(out) >= max_lines:
            break
        if len(ln) > max_chars:
            ln = ln[: max_chars - 1] + "…"
        out.append(ln)
    return "\n".join(out)


async def _submit_m1_job(recording_id: str, audio_path: str,
                         cid: str, mac: str):
    """把录音提交给 M1 Orchestrator，然后轮询直到终态并下发 OLED。"""
    try:
        async with httpx.AsyncClient(timeout=2.0) as client:
            resp = await client.post(f"{M1_BASE}/internal/v1/jobs", json={
                "device_id": mac,
                "session_id": f"device-{mac.lower().replace(':', '-')}",
                "recording_id": recording_id,
                "audio_path": audio_path,
                "codec": "pcm16",  # M0 落盘前已把 mu-law 解码为 16-bit PCM
                "sample_rate": 16000,
                "channels": 1,
            })
        # submit 用 2s（快速失败告知用户）；轮询用 5s（M1 偶发慢响应不误判）
        if resp.status_code == 409:
            # M1 单 in-flight 已满，本次录音未被接受：诚实告知，不假装处理中。
            await _display("服务忙，稍后再试", cid)
            return
        if resp.status_code not in (200, 202):
            print(f"[M1:{cid}] submit failed HTTP {resp.status_code}")
            await _display("服务暂不可用", cid)
            return
        try:
            job_id = resp.json()["job_id"]
        except (ValueError, KeyError):
            print(f"[M1:{cid}] submit bad response")
            await _display("服务暂不可用", cid)
            return
        print(f"[M1:{cid}] job {job_id} submitted (rec={recording_id})")
        await _display("处理中...", cid)
        await _poll_m1_job(job_id, cid)
    except httpx.HTTPError:
        print(f"[M1:{cid}] M1 unreachable")
        await _display("服务暂不可用", cid)


async def _poll_m1_job(job_id: str, cid: str):
    """轮询 job 状态；终态时按结果下发 OLED 事件。不打印 transcript。"""
    deadline = time.time() + M1_POLL_TIMEOUT
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            while time.time() < deadline:
                resp = await client.get(f"{M1_BASE}/internal/v1/jobs/{job_id}")
                if resp.status_code == 404:
                    print(f"[M1:{cid}] job {job_id} gone")
                    await _display("服务暂不可用", cid)
                    return
                if resp.status_code != 200:
                    print(f"[M1:{cid}] poll HTTP {resp.status_code}")
                    await asyncio.sleep(M1_POLL_INTERVAL)
                    continue
                job = resp.json()
                status = job.get("status")
                if status in ("received", "transcribing", "working", "displaying"):
                    await asyncio.sleep(M1_POLL_INTERVAL)
                    continue
                if status == "done":
                    transcript = (job.get("transcript") or "").strip()
                    reply = (job.get("reply") or "").strip()
                    if not transcript:
                        await _display("没听清...", cid)
                    else:
                        # OLED 只显示 Agent 返回（产品语义：转写是中间状态，
                        # 不回显给用户）；4 行截断。
                        text = _fit_oled(reply or "已收到")
                        await _display(text, cid)
                    print(f"[M1:{cid}] job {job_id} done")
                else:  # failed
                    print(f"[M1:{cid}] job {job_id} failed "
                          f"error_code={job.get('error_code')}")
                    await _display("服务暂不可用", cid)
                return
        print(f"[M1:{cid}] job {job_id} poll timeout")
        await _display("服务暂不可用", cid)
    except httpx.HTTPError:
        print(f"[M1:{cid}] M1 unreachable while polling {job_id}")
        await _display("服务暂不可用", cid)

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
