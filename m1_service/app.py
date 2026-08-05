"""FastAPI 应用与路由。

- GET  /healthz                 只查进程/SQLite/队列，不调外部 API
- POST /internal/v1/jobs        M0 Gateway canary 命中后提交任务
- GET  /internal/v1/jobs/{id}   查询任务状态（断线后 M0 可重查）

安全约束：
- 只监听 loopback（127.0.0.1:8786，由 uvicorn 命令行决定）
- audio_path 必须解析到配置音频根目录内，拒绝路径穿越
- 不通过 HTTP body 传音频，不做 base64
"""
from __future__ import annotations

import logging
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, HTTPException, Request, Response, status

from .config import Settings, load_settings
from .models import HealthResponse, Job, JobCreate
from .providers import agent as agent_provider
from .providers import stt as stt_provider
from .store import JobStore
from .worker import Worker

logger = logging.getLogger("despod-m1.app")


def _resolve_audio_path(audio_root: str, audio_path: str) -> str:
    """把相对/绝对路径解析到音频根目录内；穿越直接拒绝。"""
    root = Path(audio_root).resolve()
    candidate = Path(audio_path)
    if not candidate.is_absolute():
        candidate = root / candidate
    resolved = candidate.resolve()
    if not resolved.is_relative_to(root):
        raise ValueError(f"audio_path escapes audio root: {audio_path!r}")
    return str(resolved)


class M1App:
    def __init__(self, settings: Optional[Settings] = None,
                 store: Optional[JobStore] = None,
                 worker: Optional[Worker] = None):
        self.settings = settings or load_settings()
        self.store = store or JobStore(self.settings.db_path)
        self.worker = worker or Worker(
            self.store, self.settings,
            stt_fn=stt_provider.transcribe,
            agent_fn=agent_provider.chat,
        )

    def build(self) -> FastAPI:
        @asynccontextmanager
        async def lifespan(app: FastAPI):
            self.worker.start()
            logger.info("despod-m1 started (db=%s, audio_root=%s)",
                        self.settings.db_path, self.settings.audio_root)
            yield
            await self.worker.stop()

        app = FastAPI(title="despod-m1", version="0.1.0", lifespan=lifespan)

        @app.get("/healthz", response_model=HealthResponse)
        async def healthz():
            # 只检查进程、SQLite 和队列；不调用 SiliconFlow 或 Agent。
            if not self.store.health():
                raise HTTPException(status_code=503, detail="sqlite unavailable")
            return HealthResponse(queue_depth=self.worker.queue_depth)

        @app.post("/internal/v1/jobs", status_code=status.HTTP_202_ACCEPTED)
        async def create_job(create: JobCreate, request: Request):
            try:
                create.validate_codec()
            except ValueError as e:
                raise HTTPException(status_code=422, detail=str(e))
            # 幂等优先：同一 recording 重复提交返回原 job（202，契约字面
            # 「成功创建返回 202」），不重复入队，也不校验其路径。
            existing = self.store.get_by_recording(create.recording_id)
            if existing:
                return Response(
                    content=existing.model_dump_json(),
                    status_code=status.HTTP_202_ACCEPTED,
                    media_type="application/json")
            try:
                audio_path = _resolve_audio_path(
                    self.settings.audio_root, create.audio_path)
            except ValueError as e:
                raise HTTPException(status_code=400, detail=str(e))

            create = create.model_copy(update={"audio_path": audio_path})
            job = self.store.create_job(create)
            if not await self.worker.submit(job):
                # 队列满：job 已写 SQLite，显式置 failed 避免卡在 received
                # （重启恢复会重试 received，此处标记后不再重复处理）。
                self.store.set_status(job.job_id, "failed",
                                      error_code="queue_full")
                raise HTTPException(
                    status_code=409,
                    detail="queue full; one job at a time",
                )
            logger.info("job %s created (device=%s, rec=%s)",
                        job.job_id, job.device_id, job.recording_id)
            return job

        @app.get("/internal/v1/jobs/{job_id}", response_model=Job)
        async def get_job(job_id: str):
            job = self.store.get(job_id)
            if job is None:
                raise HTTPException(status_code=404, detail="job not found")
            return job

        return app


def create_app() -> FastAPI:
    return M1App().build()


# uvicorn 入口：`python -m uvicorn m1_service.app:app`（systemd 草案同款）
app = create_app()
