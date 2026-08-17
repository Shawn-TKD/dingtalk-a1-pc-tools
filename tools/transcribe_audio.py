"""Transcribe existing A1 Ogg files and store local JSON sidecars."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

from siliconflow_asr import read_api_key, transcribe_sync, write_metadata


def transcribe_file(audio_path: Path, api_key: str, model: str) -> dict:
    metadata_path = audio_path.with_suffix(".json")
    metadata = {}
    if metadata_path.exists():
        value = json.loads(metadata_path.read_text(encoding="utf-8"))
        if isinstance(value, dict):
            metadata = value
    metadata.update(
        {
            "fid": int(audio_path.stem.rsplit("-", 1)[-1]),
            "kind": metadata.get("kind") or (
                "voice_memo" if audio_path.stem.startswith("memo-") else "long_recording"
            ),
            "transcription_model": model,
            "transcribed_at": datetime.now(timezone.utc).isoformat(),
        }
    )
    try:
        metadata["transcription"] = transcribe_sync(audio_path, api_key, model)
        metadata["transcription_error"] = None
    except Exception as error:
        metadata["transcription_error"] = str(error)
        write_metadata(metadata_path, metadata)
        raise
    write_metadata(metadata_path, metadata)
    return metadata


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("audio", type=Path, nargs="+")
    parser.add_argument("--api-key-file", type=Path, default=Path(".siliconflow-api-key"))
    parser.add_argument("--model", default="FunAudioLLM/SenseVoiceSmall")
    args = parser.parse_args()
    api_key = read_api_key(args.api_key_file)
    if not api_key:
        raise RuntimeError("set SILICONFLOW_API_KEY or create the ignored local key file")
    for audio_path in args.audio:
        if audio_path.suffix.lower() != ".ogg" or not audio_path.is_file():
            raise ValueError(f"not an Ogg file: {audio_path}")
        result = transcribe_file(audio_path, api_key, args.model)
        print(
            json.dumps(
                {"file": str(audio_path), "fid": result["fid"], "text": result["transcription"]},
                ensure_ascii=False,
            )
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
