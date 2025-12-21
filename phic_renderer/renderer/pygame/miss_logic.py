from __future__ import annotations

from typing import Any, Callable, List, Optional

from ...types import NoteState


def detect_misses(
    *,
    states: List[NoteState],
    idx_next: int,
    t: float,
    miss_window: float,
    judge: Any,
    report_event_cb: Optional[Callable[[dict], Any]] = None,
):
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
            if report_event_cb is not None:
                try:
                    n = s.note
                    report_event_cb(
                        {
                            "grade": "MISS",
                            "t_now": float(t),
                            "t_hit": float(getattr(n, "t_hit", 0.0) or 0.0),
                            "note_id": int(getattr(n, "nid", si)),
                            "note_kind": int(getattr(n, "kind", 0) or 0),
                            "mh": bool(getattr(n, "mh", False)),
                            "line_id": int(getattr(n, "line_id", -1)),
                            "reason": "miss_window",
                        }
                    )
                except Exception:
                    pass
