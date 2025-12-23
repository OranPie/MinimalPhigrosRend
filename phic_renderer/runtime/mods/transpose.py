from __future__ import annotations

from typing import Any, Dict, List

from ...types import RuntimeLine, RuntimeNote
from .base import match_note_filter, parse_float


def apply_transpose(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    """Transpose mode: shift note timing forward or backward.

    Shifts all matching notes in time by a constant offset. Useful for adjusting
    chart timing or creating delayed echo effects.

    Config:
        transpose:
            enable: true
            offset: -0.5  # Time offset in seconds (negative = earlier, positive = later)
            filter:  # Optional: only transpose matching notes
                kinds: [1, 2]
    """
    cfg = None
    for k in ("transpose", "time_shift", "shift", "delay"):
        if k in mods_cfg:
            cfg = mods_cfg.get(k)
            break

    if not (isinstance(cfg, dict) and bool(cfg.get("enable", True))):
        return notes

    # Parse time offset
    time_offset = parse_float(cfg.get("offset", cfg.get("time_offset", 0)))
    if time_offset is None:
        time_offset = 0.0

    if time_offset == 0.0:
        return notes

    filter_cfg = cfg.get("filter", cfg.get("match", None))

    for n in notes:
        if n.fake:
            continue

        # Check if note matches filter
        should_transpose = True
        if isinstance(filter_cfg, dict):
            should_transpose = match_note_filter(n, filter_cfg)

        if not should_transpose:
            continue

        # Apply time offset
        n.t_hit = float(n.t_hit) + time_offset
        n.t_end = float(n.t_end) + time_offset

    # Re-sort by hit time since timing has changed
    return sorted(notes, key=lambda x: x.t_hit)
