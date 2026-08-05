"""SQLite 元数据读写。

只保存 job 元数据；PCM 音频留在文件系统，不塞进数据库。
每个操作使用独立连接（短生命周期），避免长连接跨线程问题；
单进程单 uvicorn，写入频率低，同步 sqlite3 足够。
"""
from __future__ import annotations

import sqlite3
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

from .models import Job, JobCreate

_SCHEMA = """
CREATE TABLE IF NOT EXISTS jobs (
    job_id       TEXT PRIMARY KEY,
    recording_id TEXT NOT NULL UNIQUE,
    device_id    TEXT NOT NULL,
    session_id   TEXT NOT NULL,
    audio_path   TEXT NOT NULL,
    codec        TEXT NOT NULL,
    sample_rate  INTEGER NOT NULL,
    channels     INTEGER NOT NULL,
    status       TEXT NOT NULL,
    transcript   TEXT,
    reply        TEXT,
    error_code   TEXT,
    created_at   TEXT NOT NULL,
    updated_at   TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_jobs_status ON jobs(status);
"""

_RECOVERABLE = ("received", "transcribing", "working", "displaying")
_VALID_STATUSES = _RECOVERABLE + ("done", "failed")


def _now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def _row_to_job(row: sqlite3.Row) -> Job:
    return Job(
        job_id=row["job_id"],
        recording_id=row["recording_id"],
        device_id=row["device_id"],
        session_id=row["session_id"],
        status=row["status"],
        transcript=row["transcript"],
        reply=row["reply"],
        error_code=row["error_code"],
        created_at=row["created_at"],
        updated_at=row["updated_at"],
    )


class JobStore:
    def __init__(self, db_path: str):
        self.db_path = db_path
        Path(db_path).parent.mkdir(parents=True, exist_ok=True)
        with self._connect() as conn:
            conn.executescript(_SCHEMA)

    def _connect(self) -> sqlite3.Connection:
        conn = sqlite3.connect(self.db_path, timeout=5)
        conn.row_factory = sqlite3.Row
        return conn

    def health(self) -> bool:
        """显式检查 SQLite 可读（SELECT 1），供 /healthz 使用。"""
        try:
            with self._connect() as conn:
                conn.execute("SELECT 1").fetchone()
            return True
        except sqlite3.Error:
            return False

    # ---- 写 ----

    def create_job(self, create: JobCreate) -> Job:
        """新建 job；recording_id 冲突时返回已有 job（幂等）。"""
        now = _now()
        job = Job(
            job_id=f"job-{uuid.uuid4().hex[:12]}",
            recording_id=create.recording_id,
            device_id=create.device_id,
            session_id=create.session_id,
            status="received",
            created_at=now,
            updated_at=now,
        )
        with self._connect() as conn:
            try:
                conn.execute(
                    """INSERT INTO jobs
                       (job_id, recording_id, device_id, session_id,
                        audio_path, codec, sample_rate, channels,
                        status, transcript, reply, error_code,
                        created_at, updated_at)
                       VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)""",
                    (job.job_id, job.recording_id, job.device_id,
                     job.session_id, create.audio_path, create.codec,
                     create.sample_rate, create.channels,
                     job.status, None, None, None,
                     job.created_at, job.updated_at),
                )
            except sqlite3.IntegrityError:
                return self.get_by_recording(create.recording_id)
        return job

    def set_status(self, job_id: str, status: str,
                   transcript: Optional[str] = None,
                   reply: Optional[str] = None,
                   error_code: Optional[str] = None) -> None:
        if status not in _VALID_STATUSES:
            raise ValueError(f"invalid job status: {status!r}")
        with self._connect() as conn:
            conn.execute(
                """UPDATE jobs SET status=?, transcript=?, reply=?,
                   error_code=?, updated_at=? WHERE job_id=?""",
                (status, transcript, reply, error_code, _now(), job_id),
            )

    # ---- 读 ----

    def get(self, job_id: str) -> Optional[Job]:
        with self._connect() as conn:
            row = conn.execute(
                "SELECT * FROM jobs WHERE job_id=?", (job_id,)).fetchone()
        return _row_to_job(row) if row else None

    def get_by_recording(self, recording_id: str) -> Optional[Job]:
        with self._connect() as conn:
            row = conn.execute(
                "SELECT * FROM jobs WHERE recording_id=?",
                (recording_id,)).fetchone()
        return _row_to_job(row) if row else None

    def get_audio_path(self, job_id: str) -> Optional[str]:
        with self._connect() as conn:
            row = conn.execute(
                "SELECT audio_path FROM jobs WHERE job_id=?",
                (job_id,)).fetchone()
        return row["audio_path"] if row else None

    def list_recoverable(self) -> list[Job]:
        """进程重启后需要恢复的未完成任务。"""
        marks = ",".join("?" for _ in _RECOVERABLE)
        with self._connect() as conn:
            rows = conn.execute(
                f"SELECT * FROM jobs WHERE status IN ({marks})",
                _RECOVERABLE).fetchall()
        return [_row_to_job(r) for r in rows]
