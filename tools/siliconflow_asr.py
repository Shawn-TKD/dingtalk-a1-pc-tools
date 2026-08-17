"""Small dependency-free SiliconFlow audio transcription client."""

from __future__ import annotations

import json
import os
from pathlib import Path
import urllib.error
import urllib.request
import uuid


def read_api_key(path: Path | None) -> str:
    value = os.environ.get("SILICONFLOW_API_KEY", "").strip()
    if not value and path and path.exists():
        value = path.read_text(encoding="utf-8").strip()
    return value


def transcribe_sync(audio_path: Path, api_key: str, model: str) -> str:
    boundary = f"----A1Audio{uuid.uuid4().hex}"

    def field(name: str, value: str) -> bytes:
        return (
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"{name}\"\r\n\r\n"
            f"{value}\r\n"
        ).encode("utf-8")

    body = bytearray(field("model", model))
    body.extend(
        (
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; "
            f"filename=\"{audio_path.name}\"\r\nContent-Type: audio/ogg\r\n\r\n"
        ).encode("utf-8")
    )
    body.extend(audio_path.read_bytes())
    body.extend(f"\r\n--{boundary}--\r\n".encode("ascii"))
    request = urllib.request.Request(
        "https://api.siliconflow.cn/v1/audio/transcriptions",
        data=bytes(body),
        method="POST",
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": f"multipart/form-data; boundary={boundary}",
            "Accept": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=90) as response:
            result = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", "replace")[:500]
        raise RuntimeError(f"transcription HTTP {error.code}: {detail}") from error
    text = result.get("text") if isinstance(result, dict) else None
    if not isinstance(text, str):
        raise RuntimeError("transcription response did not contain text")
    return text.strip()


def write_metadata(path: Path, value: dict) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
    temporary.replace(path)
