from __future__ import annotations

from typing import Any, Dict, List

from ...types import RuntimeLine, RuntimeNote
from .base import match_note_filter, parse_float


def apply_stretch(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    """Stretch mode: stretch or compress chart timing.

    Multiplies note timing by a factor, speeding up (<1.0) or slowing down (>1.0)
    the chart. Can be applied from a specific anchor point.

    Config:
        stretch:
            enable: true
            factor: 1.5  # Time multiplier (1.0 = no change, 2.0 = twice as slow, 0.5 = twice as fast)
            anchor: 0.0  # Anchor point in seconds (timing relative to this point is stretched)
            filter:  # Optional: only stretch matching notes
                kinds: [1, 2]
    """
    cfg = None
    for k in ("stretch", "time_stretch", "tempo", "speed_change"):
        if k in mods_cfg:
            cfg = mods_cfg.get(k)
            break

    if not (isinstance(cfg, dict) and bool(cfg.get("enable", True))):
        return notes

    # Parse stretch factor
    try:
        factor = float(cfg.get("factor", cfg.get("multiplier", 1.0)))
    except Exception:
        factor = 1.0

    if factor == 1.0:
        return notes

    # Parse anchor point
    try:
        anchor = float(cfg.get("anchor", cfg.get("anchor_time", 0.0)))
    except Exception:
        anchor = 0.0

    filter_cfg = cfg.get("filter", cfg.get("match", None))

    for n in notes:
        if n.fake:
            continue

        # Check if note matches filter
        should_stretch = True
        if isinstance(filter_cfg, dict):
            should_stretch = match_note_filter(n, filter_cfg)

        if not should_stretch:
            continue

        # Apply time stretch from anchor point
        # Formula: new_time = anchor + (old_time - anchor) * factor
        n.t_hit = anchor + (float(n.t_hit) - anchor) * factor
        n.t_end = anchor + (float(n.t_end) - anchor) * factor

    # Re-sort by hit time since timing has changed
    return sorted(notes, key=lambda x: x.t_hit)
