"""M1 Orchestrator 单元测试。

- 使用 unittest（零额外依赖）+ fastapi TestClient
- STT / Agent 全部 mock（异步 fake），不发真实网络请求
- 每个用例独立临时目录（DB + AUDIO_ROOT），互不污染
"""
from __future__ import annotations

import asyncio
import os
import tempfile
import time
import unittest
from pathlib import Path

from fastapi.testclient import TestClient

from m1_service.app import M1App
from m1_service.config import Settings
from m1_service.models import JobCreate
from m1_service.store import JobStore
from m1_service.worker import Worker


def _settings(tmp: str) -> Settings:
    return Settings(
        db_path=str(Path(tmp) / "m1_test.db"),
        audio_root=str(Path(tmp) / "audio"),
        poll_interval=0.05,
        job_timeout=10.0,
    )


def _make_audio(tmp: str, size: int = 3200) -> str:
    """在 audio_root 下造一个假 PCM 文件，返回绝对路径。"""
    root = Path(tmp) / "audio"
    root.mkdir(parents=True, exist_ok=True)
    p = root / "rec_test.raw"
    p.write_bytes(bytes([0x00, 0x00]) * (size // 2))
    return str(p)


class FakeProviders:
    """可编程 fake：记录调用次数，可注入异常/阻塞/返回值。"""

    def __init__(self, text="你好世界", reply="好的，收到。",
                 stt_error=None, agent_error=None, stt_block=None):
        self.text = text
        self.reply = reply
        self.stt_error = stt_error
        self.agent_error = agent_error
        self.stt_block = stt_block  # asyncio.Event: 不释放则卡住
        self.stt_calls = 0
        self.agent_calls = 0

    async def stt(self, audio_path, settings):
        self.stt_calls += 1
        if self.stt_block is not None:
            await self.stt_block.wait()
        if self.stt_error:
            raise self.stt_error
        return self.text

    async def agent(self, transcript, session_id, device_id, settings):
        self.agent_calls += 1
        if self.agent_error:
            raise self.agent_error
        return self.reply


class M1ServiceTestCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = self._tmp.name
        self.settings = _settings(self.tmp)
        self.audio = _make_audio(self.tmp)
        self.fake = FakeProviders()

    def tearDown(self):
        self._tmp.cleanup()

    def _client(self):
        store = JobStore(self.settings.db_path)
        self.fake_store = store
        worker = Worker(store, self.settings,
                        stt_fn=self.fake.stt, agent_fn=self.fake.agent)
        app = M1App(settings=self.settings, store=store, worker=worker)
        return TestClient(app.build())

    def _payload(self, **overrides):
        base = dict(
            device_id="14:63:93:90:CF:94",
            session_id="device-14-63-93-90-CF-94",
            recording_id="rec-uniq-1",
            audio_path=self.audio,
            codec="mulaw",
            sample_rate=16000,
            channels=1,
        )
        base.update(overrides)
        return base

    def _wait_job(self, client, job_id, timeout=5.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            r = client.get(f"/internal/v1/jobs/{job_id}")
            assert r.status_code == 200, r.text
            job = r.json()
            if job["status"] in ("done", "failed"):
                return job
            time.sleep(0.03)
        raise AssertionError(f"job {job_id} not terminal within {timeout}s")

    # ---- 验收 A.1/A.2：import / 路由 / healthz ----

    def test_healthz_ok(self):
        with self._client() as c:
            r = c.get("/healthz")
            self.assertEqual(r.status_code, 200)
            body = r.json()
            self.assertEqual(body["status"], "ok")
            self.assertEqual(body["service"], "despod-m1")
            self.assertEqual(body["queue_depth"], 0)

    # ---- 验收 A.3：正常链路 ----

    def test_job_full_flow(self):
        with self._client() as c:
            r = c.post("/internal/v1/jobs", json=self._payload())
            self.assertEqual(r.status_code, 202)
            body = r.json()
            self.assertEqual(body["status"], "received")
            job = self._wait_job(c, body["job_id"])
            self.assertEqual(job["status"], "done")
            self.assertEqual(job["transcript"], "你好世界")
            self.assertEqual(job["reply"], "好的，收到。")
            self.assertEqual(self.fake.stt_calls, 1)
            self.assertEqual(self.fake.agent_calls, 1)

    # ---- 验收 A.4：幂等 ----

    def test_duplicate_recording_id_idempotent(self):
        with self._client() as c:
            r1 = c.post("/internal/v1/jobs", json=self._payload())
            self.assertEqual(r1.status_code, 202)
            job1 = self._wait_job(c, r1.json()["job_id"])
            r2 = c.post("/internal/v1/jobs", json=self._payload())
            self.assertEqual(r2.status_code, 202)
            self.assertEqual(r2.json()["job_id"], r1.json()["job_id"])
            self.assertEqual(r2.json()["status"], job1["status"])
            self.assertEqual(self.fake.stt_calls, 1)
            self.assertEqual(self.fake.agent_calls, 1)

    def test_duplicate_while_inflight_idempotent(self):
        """处理中重复提交：返回原 job，不重复入队/调用。"""
        block = asyncio.Event()
        self.fake.stt_block = block
        with self._client() as c:
            r1 = c.post("/internal/v1/jobs", json=self._payload(
                recording_id="rec-inflight"))
            self.assertEqual(r1.status_code, 202)
            deadline = time.time() + 2
            while time.time() < deadline:
                job = c.get(
                    f"/internal/v1/jobs/{r1.json()['job_id']}").json()
                if job["status"] == "transcribing":
                    break
                time.sleep(0.02)
            r2 = c.post("/internal/v1/jobs", json=self._payload(
                recording_id="rec-inflight"))
            self.assertEqual(r2.status_code, 202)
            self.assertEqual(r2.json()["job_id"], r1.json()["job_id"])
            block.set()
            self._wait_job(c, r1.json()["job_id"])
            self.assertEqual(self.fake.stt_calls, 1)
            self.assertEqual(self.fake.agent_calls, 1)

    def test_queue_full_returns_409(self):
        block = asyncio.Event()
        self.fake.stt_block = block
        with self._client() as c:
            r1 = c.post("/internal/v1/jobs", json=self._payload(
                recording_id="rec-q1"))
            self.assertEqual(r1.status_code, 202)
            # 等第一个进入处理（卡在 stt_block）
            deadline = time.time() + 2
            while time.time() < deadline:
                job = c.get(f"/internal/v1/jobs/{r1.json()['job_id']}").json()
                if job["status"] == "transcribing":
                    break
                time.sleep(0.02)
            r2 = c.post("/internal/v1/jobs", json=self._payload(
                recording_id="rec-q2"))
            self.assertEqual(r2.status_code, 409)
            # 409 的 job 已写 SQLite 且显式 failed（不卡 received，重启不重试）
            failed_job = self.fake_store.get_by_recording("rec-q2")
            self.assertIsNotNone(failed_job)
            assert failed_job is not None
            self.assertEqual(failed_job.status, "failed")
            self.assertEqual(failed_job.error_code, "queue_full")
            # 释放，让第一个完成
            block.set()
            job = self._wait_job(c, r1.json()["job_id"])
            self.assertEqual(job["status"], "done")

    # ---- 安全：路径穿越 ----

    def test_path_traversal_rejected(self):
        with self._client() as c:
            r = c.post("/internal/v1/jobs", json=self._payload(
                recording_id="rec-trav",
                audio_path="../../etc/passwd"))
            self.assertEqual(r.status_code, 400)

    def test_invalid_codec_rejected(self):
        with self._client() as c:
            r = c.post("/internal/v1/jobs", json=self._payload(
                recording_id="rec-codec", codec="opus"))
            self.assertEqual(r.status_code, 422)

    # ---- 失败路径 ----

    def test_stt_failure_marks_failed(self):
        from m1_service.providers.stt import STTError
        self.fake.stt_error = STTError("stt_timeout", "timed out")
        with self._client() as c:
            r = c.post("/internal/v1/jobs", json=self._payload(
                recording_id="rec-sttfail"))
            job = self._wait_job(c, r.json()["job_id"])
            self.assertEqual(job["status"], "failed")
            self.assertEqual(job["error_code"], "stt_timeout")
            self.assertEqual(self.fake.agent_calls, 0)

    def test_agent_failure_marks_failed(self):
        from m1_service.providers.agent import AgentError
        self.fake.agent_error = AgentError("agent_timeout", "timed out")
        with self._client() as c:
            r = c.post("/internal/v1/jobs", json=self._payload(
                recording_id="rec-agentfail"))
            job = self._wait_job(c, r.json()["job_id"])
            self.assertEqual(job["status"], "failed")
            self.assertEqual(job["error_code"], "agent_timeout")
            self.assertEqual(self.fake.stt_calls, 1)

    def test_empty_transcript_is_done(self):
        self.fake.text = ""
        with self._client() as c:
            r = c.post("/internal/v1/jobs", json=self._payload(
                recording_id="rec-empty"))
            job = self._wait_job(c, r.json()["job_id"])
            self.assertEqual(job["status"], "done")
            self.assertEqual(job["transcript"], "")
            self.assertEqual(self.fake.agent_calls, 0)

    # ---- GET 404 ----

    def test_get_unknown_job_404(self):
        with self._client() as c:
            r = c.get("/internal/v1/jobs/job-nope")
            self.assertEqual(r.status_code, 404)

    # ---- 重启恢复 ----

    def test_recovery_requeues_incomplete_jobs(self):
        # 模拟进程崩溃：直接向 DB 写一条 received 任务
        store = JobStore(self.settings.db_path)
        job = store.create_job(JobCreate(
            device_id="14:63:93:90:CF:94",
            session_id="device-14-63-93-90-CF-94",
            recording_id="rec-recover",
            audio_path=self.audio,
            codec="mulaw",
        ))
        self.assertEqual(job.status, "received")
        # 新 Worker 启动后应恢复处理
        with self._client() as c:
            recovered = self._wait_job(c, job.job_id)
            self.assertEqual(recovered["status"], "done")
            self.assertEqual(self.fake.stt_calls, 1)


if __name__ == "__main__":
    unittest.main()
