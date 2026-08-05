"""历史 v103 音频样本真实链路测试（STT → Agent 真实 API）。

- 样本：/tmp/esp32_audio_2_1785524428_2.raw（120,706 字节 PCM，
  约 3.77 秒中文，HANDOFF 记录的 v103 短录音回归同款）
- 需要真实 key：SILICONFLOW_API_KEY / AGNES_API_KEY（环境变量注入，
  不落盘、不打印、不写仓库）；缺失时跳过。
- 运行：SILICONFLOW_API_KEY=... AGNES_API_KEY=... \
        .venv/bin/python -m unittest tests.test_m1_sample -v
- 隐私：只断言长度/行数，不打印 transcript/reply 全文。
"""
from __future__ import annotations

import os
import tempfile
import time
import unittest
from pathlib import Path

from fastapi.testclient import TestClient

from m1_service.app import M1App
from m1_service.config import Settings
from m1_service.providers import agent as agent_provider
from m1_service.providers import stt as stt_provider
from m1_service.store import JobStore
from m1_service.worker import Worker

DEFAULT_SAMPLE = "/tmp/esp32_audio_2_1785524428_2.raw"


def _has_keys() -> bool:
    return bool(os.environ.get("SILICONFLOW_API_KEY")
                and os.environ.get("AGNES_API_KEY"))


@unittest.skipUnless(_has_keys(), "需要 SILICONFLOW_API_KEY / AGNES_API_KEY 环境变量")
class RealSampleTestCase(unittest.TestCase):
    sample = os.environ.get("DESPOD_M1_SAMPLE", DEFAULT_SAMPLE)

    @classmethod
    def setUpClass(cls):
        if not Path(cls.sample).exists():
            raise unittest.SkipTest(f"样本不存在: {cls.sample}")
        cls.sample_size = Path(cls.sample).stat().st_size

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.settings = Settings(db_path=str(Path(self._tmp.name) / "m1.db"))

    def tearDown(self):
        self._tmp.cleanup()

    def test_provider_layer(self):
        """Provider 直连：STT 非空 + Agent 回复 ≤4 行。"""
        text = ""
        loop_start = time.time()
        import asyncio
        text = asyncio.run(stt_provider.transcribe(self.sample, self.settings))
        self.assertGreater(len(text), 0, "STT 应返回非空文本")
        reply = asyncio.run(agent_provider.chat(
            text, "device-test", "14:63:93:90:CF:94", self.settings))
        self.assertGreater(len(reply), 0, "Agent 应返回非空回复")
        self.assertLessEqual(len(reply.split("\n")), 4,
                             "回复应 ≤4 行（OLED 约束）")
        print(f"[sample] size={self.sample_size}B transcript={len(text)}字 "
              f"reply={len(reply)}字 lines={len(reply.split(chr(10)))} "
              f"took={time.time()-loop_start:.1f}s")

    def test_full_job_flow(self):
        """完整 job 链路：POST → done，transcript/reply 非空。"""
        store = JobStore(self.settings.db_path)
        worker = Worker(store, self.settings,
                        stt_fn=stt_provider.transcribe,
                        agent_fn=agent_provider.chat)
        app = M1App(settings=self.settings, store=store, worker=worker)
        with TestClient(app.build()) as c:
            r = c.post("/internal/v1/jobs", json={
                "device_id": "14:63:93:90:CF:94",
                "session_id": "device-14-63-93-90-CF-94",
                "recording_id": "rec-sample-e2e",
                "audio_path": self.sample,
                "codec": "pcm16",
                "sample_rate": 16000,
                "channels": 1,
            })
            self.assertEqual(r.status_code, 202)
            job_id = r.json()["job_id"]
            deadline = time.time() + 90
            job = None
            while time.time() < deadline:
                job = c.get(f"/internal/v1/jobs/{job_id}").json()
                if job["status"] in ("done", "failed"):
                    break
                time.sleep(0.5)
            self.assertIsNotNone(job, "job 未在 90s 内到达终态")
            assert job is not None
            self.assertEqual(job["status"], "done",
                             f"job failed: {job.get('error_code')}")
            self.assertGreater(len(job["transcript"] or ""), 0)
            self.assertGreater(len(job["reply"] or ""), 0)
            self.assertLessEqual(len(job["reply"].split("\n")), 4)
            print(f"[e2e] job={job_id} transcript={len(job['transcript'])}字 "
                  f"reply={len(job['reply'])}字")


if __name__ == "__main__":
    unittest.main()
