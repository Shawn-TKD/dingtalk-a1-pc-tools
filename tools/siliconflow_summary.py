"""Small dependency-free SiliconFlow chat-completions client for recording summaries."""

from __future__ import annotations

import json
import urllib.error
import urllib.request


SYSTEM_PROMPT = """你是一名谨慎的中文录音整理助手。只根据转录原文整理，不补充原文没有的信息。
输出简洁的 Markdown，并严格使用以下结构：
## 摘要
用一段话概括核心内容。
## 要点
- 列出重要信息；没有则写“无”。
## 待办
- 列出明确的行动、负责人和时间；原文未说明的字段不要猜测。没有则写“无”。
## 标签
用 3—6 个短标签，以中文逗号分隔。"""


def summarize_sync(transcription: str, api_key: str, model: str) -> str:
    text = transcription.strip()
    if not text:
        raise ValueError("transcription is empty")
    payload = json.dumps(
        {
            "model": model,
            "messages": [
                {"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": f"请整理这段录音转录：\n\n{text}"},
            ],
            "temperature": 0.2,
            "max_tokens": 1600,
            "enable_thinking": False,
        },
        ensure_ascii=False,
    ).encode("utf-8")
    request = urllib.request.Request(
        "https://api.siliconflow.cn/v1/chat/completions",
        data=payload,
        method="POST",
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
            "Accept": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=90) as response:
            result = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", "replace")[:500]
        raise RuntimeError(f"summary HTTP {error.code}: {detail}") from error
    try:
        content = result["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError) as error:
        raise RuntimeError("summary response did not contain content") from error
    if not isinstance(content, str) or not content.strip():
        raise RuntimeError("summary response was empty")
    return content.strip()
