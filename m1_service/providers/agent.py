"""Agent Gateway 适配器（Agnes）。

- 输入：transcript + session_id + device_id
- 输出：适合 128x64 OLED 的紧凑回复（system prompt 约束约 36 个显示字符）
- 约束：异步 HTTP、显式超时、有限重试、错误码归一；
  不传 max_tokens（显式小值会被 thinking token 占满导致 content 为空），
  长度由 system prompt 约束。
"""
from __future__ import annotations

import asyncio

import httpx

from ..config import Settings

AGNES_SYSTEM = (
    "你是 Desktoppy 桌面助手，回复会显示在 128x64 OLED 上。"
    "只输出给用户的最终答复，不要自我介绍、角色说明、Markdown、emoji 或引号。"
    "直接说结论或下一步，口语化、紧凑；最多约 36 个中文等宽字符，尽量不超过 3 行。"
    "如果内容过长，只保留最重要的信息。听不清时只说：没听清，再说一次？"
)


class AgentError(Exception):
    """Agent 调用失败。code 为归一错误码。"""

    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


async def chat(transcript: str, session_id: str, device_id: str,
               settings: Settings) -> str:
    if not settings.agnes_api_key:
        raise AgentError("config_error", "AGNES_API_KEY not set")

    url = f"{settings.agnes_base_url.rstrip('/')}/chat/completions"
    headers = {
        "Authorization": f"Bearer {settings.agnes_api_key}",
        "Content-Type": "application/json",
    }
    payload = {
        "model": settings.agnes_model,
        "messages": [
            {"role": "system", "content": AGNES_SYSTEM},
            {"role": "user", "content": transcript},
        ],
        "temperature": 0.7,
    }

    last_err: Exception | None = None
    for attempt in range(settings.agent_max_retries + 1):
        try:
            async with httpx.AsyncClient(timeout=settings.agent_timeout) as client:
                resp = await client.post(url, headers=headers, json=payload)
            if resp.status_code == 429:
                last_err = AgentError("rate_limited", "Agent rate limited")
            elif resp.status_code >= 400:
                last_err = AgentError(
                    "agent_http_error", f"Agent HTTP {resp.status_code}")
            else:
                content = resp.json()["choices"][0]["message"]["content"]
                return (content or "").strip()
        except httpx.TimeoutException:
            last_err = AgentError("agent_timeout", "Agent request timed out")
        except httpx.HTTPError as e:
            last_err = AgentError(
                "agent_network_error", f"Agent network error: {type(e).__name__}")
        except (KeyError, IndexError, ValueError):
            last_err = AgentError("agent_bad_response", "Agent response malformed")
        if attempt < settings.agent_max_retries:
            await asyncio.sleep(1.0 * (attempt + 1))
    raise last_err  # type: ignore[misc]
