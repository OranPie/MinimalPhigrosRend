from __future__ import annotations

from typing import Any

from .base import AudioBackend


def create_audio_backend(name: str, **kwargs: Any) -> AudioBackend:
    nm = str(name or "pygame").strip().lower()
    if nm == "pygame":
        from .backends.pygame_audio import PygameAudio
        return PygameAudio(**kwargs)
    if nm in {"openal", "al"}:
        from .backends.openal_audio import OpenALAudio
        return OpenALAudio(**kwargs)
    raise SystemExit(f"Unknown audio backend: {name}")
