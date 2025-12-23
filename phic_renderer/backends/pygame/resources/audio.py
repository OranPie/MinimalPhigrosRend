from __future__ import annotations

import os
from typing import Any, Dict, Optional

from ....types import RuntimeNote


def _key_for_note_kind(kind: int) -> str:
    if kind == 1:
        return "click"
    if kind == 2:
        return "drag"
    if kind == 4:
        return "flick"
    if kind == 3:
        return "click"
    return "click"


class HitsoundPlayer:
    def __init__(self, *, audio: Any, chart_dir: str, min_interval_ms: int = 0):
        self.audio = audio
        self.chart_dir = chart_dir
        self.min_interval_ms = max(0, int(min_interval_ms))
        self.last_hitsound_ms: Dict[str, int] = {}
        self.custom_sfx_cache: Dict[str, Any] = {}

    def play(self, note: RuntimeNote, now_tick: int, *, respack: Optional[Any]):
        if note.hitsound_path:
            fp = str(note.hitsound_path)
            if not os.path.isabs(fp):
                fp = os.path.join(self.chart_dir, fp)
            if os.path.exists(fp):
                if self.min_interval_ms > 0:
                    last = self.last_hitsound_ms.get(fp, -10**9)
                    if now_tick - last < self.min_interval_ms:
                        return
                try:
                    snd = self.custom_sfx_cache.get(fp)
                    if snd is None:
                        snd = self.audio.load_sound(fp)
                        self.custom_sfx_cache[fp] = snd
                    self.audio.play_sound(snd)
                    self.last_hitsound_ms[fp] = now_tick
                    return
                except:
                    pass

        if not respack:
            return

        key = _key_for_note_kind(int(note.kind))
        if self.min_interval_ms > 0:
            last = self.last_hitsound_ms.get(key, -10**9)
            if now_tick - last < self.min_interval_ms:
                return

        snd = respack.sfx.get(key)
        if snd:
            self.audio.play_sound(snd)
            self.last_hitsound_ms[key] = now_tick
