"""有界队列与任务状态机。

- asyncio.Queue(maxsize=1)：单设备单 in-flight job，队列满由调用方返回 409
- 状态机：received -> transcribing -> working -> displaying -> done
  任何阶段失败 -> failed，error_code 归一，不静默吞错
- 启动时扫描 SQLite 中未完成任务重新入队（进程重启恢复）
- STT / Agent 通过依赖注入传入，便于单元测试 mock
"""
from __future__ import annotations

import asyncio
import logging
from pathlib import Path
from typing import Awaitable, Callable, Optional

from .config import Settings
from .models import Job
from .store import JobStore

logger = logging.getLogger("despod-m1.worker")

STT_FN = Callable[[str, Settings], Awaitable[str]]
AGENT_FN = Callable[[str, str, str, Settings], Awaitable[str]]


class Worker:
    def __init__(self, store: JobStore, settings: Settings,
                 stt_fn: STT_FN, agent_fn: AGENT_FN):
        self.store = store
        self.settings = settings
        self.stt_fn = stt_fn
        self.agent_fn = agent_fn
        self.queue: asyncio.Queue[Job] = asyncio.Queue(
            maxsize=settings.queue_maxsize)
        # in-flight 计数 = 排队中 + 处理中；达到 maxsize 即拒绝新任务。
        # 仅用 Queue(maxsize) 时处理中的任务已出队，实际并发上限会变成
        # maxsize+1，违反「单设备单 in-flight + 队列满 409」的契约。
        self.active = 0
        self._task: Optional[asyncio.Task] = None

    @property
    def queue_depth(self) -> int:
        return self.queue.qsize()

    def start(self) -> None:
        if self._task is None or self._task.done():
            self._task = asyncio.create_task(self._run())

    async def stop(self) -> None:
        if self._task is not None:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
            self._task = None

    async def submit(self, job: Job) -> bool:
        """入队；in-flight 已满返回 False（调用方回 409），不丢任务。"""
        if self.active >= self.settings.queue_maxsize:
            return False
        self.active += 1
        try:
            self.queue.put_nowait(job)
            return True
        except asyncio.QueueFull:
            self.active -= 1
            return False

    async def _run(self) -> None:
        # 重启恢复：把未完成任务重新入队（先入先处理）。
        for job in self.store.list_recoverable():
            if self.active >= self.settings.queue_maxsize:
                logger.warning(
                    "in-flight full during recovery; job %s stays queued in DB",
                    job.job_id)
                break
            self.active += 1
            try:
                self.queue.put_nowait(job)
                logger.info("recovered job %s (status=%s)",
                            job.job_id, job.status)
            except asyncio.QueueFull:
                self.active -= 1
                break
        while True:
            job = await self.queue.get()
            try:
                await self._process(job)
            except asyncio.CancelledError:
                raise
            except Exception:  # 防御性兜底，状态机内部已捕获具体错误
                logger.exception("unexpected worker error for %s", job.job_id)
            finally:
                self.queue.task_done()
                self.active -= 1

    async def _process(self, job: Job) -> None:
        settings = self.settings
        try:
            # received -> transcribing
            self.store.set_status(job.job_id, "transcribing")
            audio_path = self.store.get_audio_path(job.job_id)
            if not audio_path:
                self.store.set_status(job.job_id, "failed",
                                      error_code="io_error")
                return
            text = await self.stt_fn(audio_path, settings)
            text = (text or "").strip()
            if not text:
                # 空转录不是错误：done + transcript=""（M0 显示「没听清」）。
                # 不给 Agent 传空文本（与已验证的 M1 实验行为一致）。
                self.store.set_status(job.job_id, "done", transcript="")
                logger.info("job %s done (empty transcript)", job.job_id)
                return

            # transcribing -> working（保存 transcript）
            self.store.set_status(job.job_id, "working", transcript=text)
            reply = await self.agent_fn(text, job.session_id, job.device_id,
                                        settings)

            # working -> displaying -> done
            self.store.set_status(job.job_id, "displaying",
                                  transcript=text, reply=reply)
            self.store.set_status(job.job_id, "done",
                                  transcript=text, reply=reply)
            logger.info("job %s done", job.job_id)
        except Exception as e:
            code = getattr(e, "code", "unknown_error")
            self.store.set_status(job.job_id, "failed", error_code=code)
            logger.warning("job %s failed: %s", job.job_id, code)
