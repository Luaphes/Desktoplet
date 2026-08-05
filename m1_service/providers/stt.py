"""SiliconFlow STT 适配器。

- 输入：16-bit 单声道 PCM（M0 ws_server 落盘格式，mu-law 已解码）
- 输出：转录文本
- 约束：异步 HTTP、显式超时、有限重试、错误码归一，不打印密钥与音频内容
"""
from __future__ import annotations

import asyncio
import io
import wave
from pathlib import Path

import httpx

from ..config import Settings


class STTError(Exception):
    """STT 调用失败（网络/HTTP/解析）。code 为归一错误码。"""

    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def _pcm_to_wav(pcm: bytes, sample_rate: int) -> bytes:
    """把 16-bit mono PCM 包进最小 WAV 容器。"""
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(pcm)
    return buf.getvalue()


async def transcribe(audio_path: str, settings: Settings) -> str:
    if not settings.sf_api_key:
        raise STTError("config_error", "SILICONFLOW_API_KEY not set")
    pcm = Path(audio_path).read_bytes()
    if not pcm:
        raise STTError("io_error", "empty audio file")
    wav = _pcm_to_wav(pcm, settings.sample_rate)

    url = f"{settings.sf_base_url.rstrip('/')}/audio/transcriptions"
    headers = {"Authorization": f"Bearer {settings.sf_api_key}"}
    files = {"file": ("audio.wav", wav, "audio/wav")}
    data = {"model": settings.stt_model, "response_format": "json"}

    last_err: Exception | None = None
    for attempt in range(settings.stt_max_retries + 1):
        try:
            async with httpx.AsyncClient(timeout=settings.stt_timeout) as client:
                resp = await client.post(url, headers=headers,
                                         files=files, data=data)
            if resp.status_code == 429:
                last_err = STTError("rate_limited", "STT rate limited")
            elif resp.status_code >= 400:
                last_err = STTError(
                    "stt_http_error", f"STT HTTP {resp.status_code}")
            else:
                text = resp.json().get("text", "").strip()
                return text
        except httpx.TimeoutException:
            last_err = STTError("stt_timeout", "STT request timed out")
        except httpx.HTTPError as e:
            last_err = STTError("stt_network_error", f"STT network error: {type(e).__name__}")
        except ValueError:
            last_err = STTError("stt_bad_response", "STT response not JSON")
        if attempt < settings.stt_max_retries:
            await asyncio.sleep(1.0 * (attempt + 1))
    raise last_err  # type: ignore[misc]
