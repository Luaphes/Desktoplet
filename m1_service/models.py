"""job / event 数据模型。"""
from __future__ import annotations

from typing import Literal, Optional

from pydantic import BaseModel, Field

# 状态机只允许单向流转：
#   received -> transcribing -> working -> displaying -> done
# 任何阶段都可以进入 failed；不得静默吞错。
JobStatus = Literal[
    "received", "transcribing", "working", "displaying", "done", "failed"
]

_VALID_CODECS = {"pcm16", "mulaw"}


class JobCreate(BaseModel):
    """M0 Gateway 在 mic_stop 且 canary 命中后提交的任务。"""
    device_id: str = Field(min_length=1)
    session_id: str = Field(min_length=1)
    recording_id: str = Field(min_length=1)
    audio_path: str = Field(min_length=1)
    codec: str = "pcm16"
    sample_rate: int = 16000
    channels: int = 1

    def validate_codec(self) -> None:
        if self.codec not in _VALID_CODECS:
            raise ValueError(f"unsupported codec: {self.codec!r}")


class Job(BaseModel):
    job_id: str
    recording_id: str
    device_id: str
    session_id: str
    status: JobStatus
    transcript: Optional[str] = None
    reply: Optional[str] = None
    error_code: Optional[str] = None
    created_at: str
    updated_at: str


class HealthResponse(BaseModel):
    status: Literal["ok"] = "ok"
    service: Literal["despod-m1"] = "despod-m1"
    queue_depth: int
