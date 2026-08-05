"""ws_server.py M1 canary 集成测试。

- _fit_oled：OLED 4 行截断
- _submit_m1_job / _poll_m1_job：真实 HTTP 打到本进程内起的测试版 M1
  （随机端口 + fake providers），验证 OLED 事件序列与降级路径。
- 不依赖 8765（线上 despod.service 占用），不触碰线上服务。
"""
from __future__ import annotations

import asyncio
import json
import socket
import tempfile
import threading
import time
import unittest
from pathlib import Path

import uvicorn

import ws_server
from m1_service.app import M1App
from m1_service.config import Settings
from m1_service.store import JobStore
from m1_service.worker import Worker
from tests.test_m1_service import FakeProviders


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class _M1ServerThread(threading.Thread):
    """后台线程跑一个测试版 M1（随机端口 + fake providers）。"""

    def __init__(self, fake: FakeProviders):
        super().__init__(daemon=True)
        self.fake = fake
        self.port = _free_port()

    def run(self):
        tmp = tempfile.mkdtemp(prefix="m1-canary-")
        settings = Settings(
            db_path=str(Path(tmp) / "m1.db"),
            audio_root="/tmp",  # 与真实配置一致（ws_server 落盘目录）
        )
        store = JobStore(settings.db_path)
        worker = Worker(store, settings,
                        stt_fn=self.fake.stt, agent_fn=self.fake.agent)
        app = M1App(settings=settings, store=store, worker=worker).build()
        config = uvicorn.Config(app, host="127.0.0.1", port=self.port,
                                log_level="warning")
        server = uvicorn.Server(config)
        server.run()


class WsCanaryTestCase(unittest.TestCase):
    def setUp(self):
        self.fake = FakeProviders(text="帮我查一下天气", reply="好的，稍等。")

    def _start_m1(self):
        th = _M1ServerThread(self.fake)
        th.start()
        # 等端口就绪（uvicorn 绑定完成）
        deadline = time.time() + 5
        while time.time() < deadline:
            try:
                with socket.create_connection(("127.0.0.1", th.port),
                                              timeout=0.2):
                    break
            except OSError:
                time.sleep(0.05)
        else:
            raise AssertionError("test M1 server did not start")
        return th

    def _capture_safe_send(self):
        captured = []

        async def fake_safe_send(msg, target=None):
            captured.append(msg)

        self._orig_safe_send = ws_server._safe_send
        ws_server._safe_send = fake_safe_send
        return captured

    def tearDown(self):
        ws_server._safe_send = getattr(
            self, "_orig_safe_send", ws_server._safe_send)

    def _display_texts(self, captured):
        return [json.loads(m)["text"] for m in captured]

    # ---- 纯函数：OLED 截断 ----

    def test_fit_oled_lines_and_chars(self):
        text = ("你说：这是一句很长很长的中文转写内容\n\n"
                "回复第一行\n回复第二行\n回复第三行\n回复第四行\n回复第五行")
        out = ws_server._fit_oled(text)
        self.assertLessEqual(len(out.split("\n")), 4)
        for ln in out.split("\n"):
            self.assertLessEqual(len(ln), 12)
        self.assertIn("…", out)

    def test_fit_oled_short_text_unchanged(self):
        text = "你说：你好\n\n好的"
        self.assertEqual(ws_server._fit_oled(text), text)

    # ---- canary 提交/轮询（打测试版 M1）----

    def test_submit_and_poll_done(self):
        th = self._start_m1()
        captured = self._capture_safe_send()

        async def run():
            ws_server.M1_BASE = f"http://127.0.0.1:{th.port}"
            await ws_server._submit_m1_job(
                "rec-canary-1", "/tmp/esp32_audio_2_1785524428_2.raw",
                "0", ws_server.CANARY_MAC)

        asyncio.run(run())
        texts = self._display_texts(captured)
        self.assertIn("处理中...", texts)
        done = [t for t in texts if t.startswith("你说：")]
        self.assertEqual(len(done), 1)
        self.assertEqual(self.fake.stt_calls, 1)
        self.assertEqual(self.fake.agent_calls, 1)

    def test_submit_unreachable_shows_unavailable(self):
        captured = self._capture_safe_send()

        async def run():
            ws_server.M1_BASE = "http://127.0.0.1:1"  # 必然不可达
            await ws_server._submit_m1_job(
                "rec-canary-2", "/tmp/x.raw", "0", ws_server.CANARY_MAC)

        asyncio.run(run())
        texts = self._display_texts(captured)
        self.assertIn("服务暂不可用", texts)

    # ---- poll 降级路径：404 / 超时 ----

    def _start_fake_m1(self, handler):
        """http.server 线程：按 job_id 返回固定响应。"""
        import http.server

        class H(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                body = handler(self.path)
                code, payload = body
                data = json.dumps(payload).encode()
                self.send_response(code)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def log_message(self, format, *args):
                pass

        port = _free_port()
        srv = http.server.HTTPServer(("127.0.0.1", port), H)
        th = threading.Thread(target=srv.serve_forever, daemon=True)
        th.start()
        return srv, port

    def test_poll_404_shows_unavailable(self):
        srv, port = self._start_fake_m1(
            lambda path: (404, {"detail": "job not found"}))
        captured = self._capture_safe_send()

        async def run():
            ws_server.M1_BASE = f"http://127.0.0.1:{port}"
            await ws_server._poll_m1_job("job-gone", "0")

        asyncio.run(run())
        srv.shutdown()
        texts = self._display_texts(captured)
        self.assertIn("服务暂不可用", texts)

    def test_poll_deadline_timeout_shows_unavailable(self):
        srv, port = self._start_fake_m1(
            lambda path: (200, {"status": "received"}))
        captured = self._capture_safe_send()

        async def run():
            ws_server.M1_BASE = f"http://127.0.0.1:{port}"
            ws_server.M1_POLL_TIMEOUT = 0.3  # 快速触发 deadline
            await ws_server._poll_m1_job("job-slow", "0")

        asyncio.run(run())
        srv.shutdown()
        texts = self._display_texts(captured)
        self.assertIn("服务暂不可用", texts)


if __name__ == "__main__":
    unittest.main()
