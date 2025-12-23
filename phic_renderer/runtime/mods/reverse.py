from __future__ import annotations

from typing import Any, Dict, List

from ...types import RuntimeLine, RuntimeNote
from .base import match_note_filter


def apply_reverse(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    """Reverse mode: reverse note order in time.

    Mirrors the chart timeline, making notes play in reverse order. The last note
    becomes the first, and vice versa.

    Config:
        reverse:
            enable: true
            anchor: null  # Anchor point in seconds (default: use midpoint of all notes)
            preserve_holds: true  # Keep hold durations correct (swap t_hit and t_end)
            filter:  # Optional: only reverse matching notes
                kinds: [1, 2, 4]
    """
    cfg = None
    for k in ("reverse", "backwards", "invert_time"):
        if k in mods_cfg:
            cfg = mods_cfg.get(k)
            break

    if not (isinstance(cfg, dict) and bool(cfg.get("enable", True))):
        return notes

    # Parse anchor point
    anchor = cfg.get("anchor", cfg.get("anchor_time", None))
    preserve_holds = bool(cfg.get("preserve_holds", True))
    filter_cfg = cfg.get("filter", cfg.get("match", None))

    # Find notes that match filter
    matching_notes = []
    for n in notes:
        if n.fake:
            continue

        should_reverse = True
        if isinstance(filter_cfg, dict):
            should_reverse = match_note_filter(n, filter_cfg)

        if should_reverse:
            matching_notes.append(n)

    if not matching_notes:
        return notes

    # Calculate anchor point if not specified
    if anchor is None:
        try:
            min_t = min(float(n.t_hit) for n in matching_notes)
            max_t = max(float(n.t_end) for n in matching_notes)
            anchor = (min_t + max_t) / 2.0
        except Exception:
            anchor = 0.0
    else:
        try:
            anchor = float(anchor)
        except Exception:
            anchor = 0.0

    # Reverse matching notes
    for n in matching_notes:
        old_t_hit = float(n.t_hit)
        old_t_end = float(n.t_end)

        # Mirror around anchor point
        new_t_hit = anchor - (old_t_hit - anchor)
        new_t_end = anchor - (old_t_end - anchor)

        if preserve_holds and int(n.kind) == 3:
            # For holds, swap hit and end times to preserve duration
            n.t_hit = min(new_t_hit, new_t_end)
            n.t_end = max(new_t_hit, new_t_end)
        else:
            n.t_hit = new_t_hit
            n.t_end = new_t_end

    # Re-sort by hit time since timing has changed
    return sorted(notes, key=lambda x: x.t_hit)
