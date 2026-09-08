"""Provider-independent voice-mode routing. This module never executes speech as code."""
from dataclasses import dataclass
import re


MODES = {"开发模式": "development", "灵感模式": "ideas", "调研模式": "research",
         "复盘模式": "review", "游戏模式": "game"}


@dataclass
class ModeRouter:
    mode: str = "ideas"

    def route(self, transcript: str, *, recording_key=None):
        text = transcript.strip()
        # Only an explicit prefix changes mode; quotes/negation later in speech don't.
        match = re.match(r"^(?:切换到|进入)?(开发模式|灵感模式|调研模式|复盘模式|游戏模式)(?:[，。,:：！!\s]+|$)", text)
        if match:
            self.mode = MODES[match[1]]
            text = text[match.end():].strip()
        return {"event": "agent_task" if text else "mode_changed", "mode": self.mode,
                "text": text, "recording_key": recording_key,
                "executed": False, "input_source": "audio_transcription"}
