from __future__ import annotations

from typing import Any, List

from ...types import NoteState


def detect_misses(*, states: List[NoteState], idx_next: int, t: float, miss_window: float, judge: Any):
    st0 = max(0, int(idx_next) - 200)
    st1 = min(len(states), int(idx_next) + 800)
    for si in range(st0, st1):
        s = states[si]
        if s.judged or s.note.fake:
            continue
        if s.note.kind == 3:
            continue
        if float(t) > float(s.note.t_hit) + float(miss_window):
            try:
                setattr(s, "miss_t", float(t))
            except Exception:
                pass
            judge.mark_miss(s)
