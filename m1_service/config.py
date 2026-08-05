"""M1 Orchestrator 配置。

密钥只从环境变量 / EnvironmentFile 读取（/etc/despod-m1.env），
不读 Hermes config、不写仓库。所有敏感值缺省为空字符串，
服务仍可启动（healthz 不依赖密钥），job 处理时因配置缺失进入 failed。
"""
from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path


def _env(name: str, default: str = "") -> str:
    return os.environ.get(name, default).strip()


@dataclass(frozen=True)
class Settings:
    # --- 路径 ---
    db_path: str = field(default_factory=lambda: _env(
        "DESPOD_M1_DB_PATH", "/root/Desktoppy/data/m1_jobs.db"))
    audio_root: str = field(default_factory=lambda: _env(
        "DESPOD_M1_AUDIO_ROOT", "/tmp"))
    # --- 队列 ---
    queue_maxsize: int = field(default_factory=lambda: int(
        _env("DESPOD_M1_QUEUE_MAXSIZE", "1")))
    poll_interval: float = field(default_factory=lambda: float(
        _env("DESPOD_M1_POLL_INTERVAL", "0.5")))
    job_timeout: float = field(default_factory=lambda: float(
        _env("DESPOD_M1_JOB_TIMEOUT", "60")))
    # --- SiliconFlow STT ---
    sf_api_key: str = field(default_factory=lambda: _env(
        "SILICONFLOW_API_KEY"))
    sf_base_url: str = field(default_factory=lambda: _env(
        "SILICONFLOW_BASE_URL", "https://api.siliconflow.cn/v1"))
    stt_model: str = field(default_factory=lambda: _env(
        "DESPOD_M1_STT_MODEL", "FunAudioLLM/SenseVoiceSmall"))
    stt_timeout: float = field(default_factory=lambda: float(
        _env("DESPOD_M1_STT_TIMEOUT", "30")))
    stt_max_retries: int = field(default_factory=lambda: int(
        _env("DESPOD_M1_STT_MAX_RETRIES", "2")))
    # --- Agent Gateway (Agnes) ---
    agnes_api_key: str = field(default_factory=lambda: _env(
        "AGNES_API_KEY"))
    agnes_base_url: str = field(default_factory=lambda: _env(
        "AGNES_BASE_URL", "https://api.agnes-ai.cn/v1"))
    agnes_model: str = field(default_factory=lambda: _env(
        "DESPOD_M1_AGENT_MODEL", "agnes-2.5-flash"))
    agent_timeout: float = field(default_factory=lambda: float(
        _env("DESPOD_M1_AGENT_TIMEOUT", "15")))
    agent_max_retries: int = field(default_factory=lambda: int(
        _env("DESPOD_M1_AGENT_MAX_RETRIES", "2")))
    # --- 音频 ---
    sample_rate: int = field(default_factory=lambda: int(
        _env("DESPOD_M1_SAMPLE_RATE", "16000")))


def load_settings() -> Settings:
    return Settings()
