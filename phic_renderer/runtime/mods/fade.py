from __future__ import annotations

from typing import Any, Dict, List

from ...types import RuntimeLine, RuntimeNote
from .base import match_note_filter, parse_float


def apply_fade(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    """Fade mode: fade notes in/out based on time or other criteria.

    Example: fade in notes at the start, fade out at the end, or create gradual alpha changes.

    Config:
        fade:
            enable: true
            mode: "time"  # "time", "linear", "constant" (default: "time")
            time_start: 0.0  # Start time for fade in (default: None)
            time_end: 10.0  # End time for fade out (default: None)
            alpha_start: 0.0  # Alpha at start time (default: 0.0)
            alpha_end: 1.0  # Alpha at end time (default: 1.0)
            alpha_min: 0.1  # Minimum alpha clamp (default: 0.0)
            alpha_max: 1.0  # Maximum alpha clamp (default: 1.0)
            constant_alpha: 0.5  # Constant alpha for "constant" mode
            filter:  # Optional: only fade matching notes
                kinds: [1, 2, 4]
    """
    cfg = None
    for k in ("fade", "alpha", "opacity"):
        if k in mods_cfg:
            cfg = mods_cfg.get(k)
            break

    if not (isinstance(cfg, dict) and bool(cfg.get("enable", True))):
        return notes

    # Parse mode
    mode = str(cfg.get("mode", "time")).strip().lower()

    # Parse time ranges
    time_start = parse_float(cfg.get("time_start", cfg.get("fade_in_start", None)))
    time_end = parse_float(cfg.get("time_end", cfg.get("fade_out_end", None)))

    # Parse alpha values
    try:
        alpha_start = float(cfg.get("alpha_start", 0.0))
    except Exception:
        alpha_start = 0.0

    try:
        alpha_end = float(cfg.get("alpha_end", 1.0))
    except Exception:
        alpha_end = 1.0

    try:
        alpha_min = float(cfg.get("alpha_min", 0.0))
    except Exception:
        alpha_min = 0.0

    try:
        alpha_max = float(cfg.get("alpha_max", 1.0))
    except Exception:
        alpha_max = 1.0

    try:
        constant_alpha = float(cfg.get("constant_alpha", cfg.get("alpha", 0.5)))
    except Exception:
        constant_alpha = 0.5

    filter_cfg = cfg.get("filter", cfg.get("match", None))

    for n in notes:
        if n.fake:
            continue

        # Check if note matches filter
        should_fade = True
        if isinstance(filter_cfg, dict):
            should_fade = match_note_filter(n, filter_cfg)

        if not should_fade:
            continue

        # Apply fade based on mode
        if mode == "constant":
            # Set constant alpha
            n.alpha01 = max(alpha_min, min(alpha_max, constant_alpha))
        elif mode == "time":
            # Fade based on time ranges
            t = float(n.t_hit)
            new_alpha = float(n.alpha01)

            if time_start is not None and time_end is not None:
                # Full fade in/out range
                if t <= time_start:
                    new_alpha = alpha_start
                elif t >= time_end:
                    new_alpha = alpha_end
                else:
                    # Linear interpolation
                    progress = (t - time_start) / (time_end - time_start)
                    new_alpha = alpha_start + (alpha_end - alpha_start) * progress
            elif time_start is not None:
                # Fade in only
                if t <= time_start:
                    new_alpha = alpha_start
                else:
                    # Could add fade-in duration, for now just set to current alpha
                    pass
            elif time_end is not None:
                # Fade out only
                if t >= time_end:
                    new_alpha = alpha_end
                else:
                    # Could add fade-out duration, for now just set to current alpha
                    pass

            n.alpha01 = max(alpha_min, min(alpha_max, new_alpha))
        elif mode == "linear":
            # Linear fade across all notes (by index)
            # This would require sorting and indexing - for simplicity, use time-based
            # Fall back to time-based for now
            pass

    return notes
