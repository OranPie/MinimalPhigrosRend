from __future__ import annotations

from typing import Any, Dict, List

from ...types import RuntimeLine, RuntimeNote
from .base import match_note_filter, parse_float


def apply_scale(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    """Scale mode: scale note sizes, speeds, or positions.

    Example: make all notes 150% larger, or make notes spread out more horizontally.

    Config:
        scale:
            enable: true
            size: 1.5  # Size multiplier (default: 1.0)
            speed: 1.2  # Speed multiplier (default: 1.0)
            x: 1.5  # X position multiplier from center (default: 1.0)
            y: 1.0  # Y position multiplier (default: 1.0)
            x_center: 0  # Center point for X scaling (default: 0)
            y_center: 0  # Center point for Y scaling (default: 0)
            filter:  # Optional: only scale matching notes
                kinds: [1, 2, 4]  # Scale tap, drag, and flick
    """
    cfg = None
    for k in ("scale", "zoom", "resize"):
        if k in mods_cfg:
            cfg = mods_cfg.get(k)
            break

    if not (isinstance(cfg, dict) and bool(cfg.get("enable", True))):
        return notes

    # Parse multipliers
    try:
        size_mul = float(cfg.get("size", cfg.get("size_mul", 1.0)))
    except Exception:
        size_mul = 1.0

    try:
        speed_mul = float(cfg.get("speed", cfg.get("speed_mul", 1.0)))
    except Exception:
        speed_mul = 1.0

    try:
        x_mul = float(cfg.get("x", cfg.get("x_scale", 1.0)))
    except Exception:
        x_mul = 1.0

    try:
        y_mul = float(cfg.get("y", cfg.get("y_scale", 1.0)))
    except Exception:
        y_mul = 1.0

    try:
        x_center = float(cfg.get("x_center", 0))
    except Exception:
        x_center = 0.0

    try:
        y_center = float(cfg.get("y_center", 0))
    except Exception:
        y_center = 0.0

    filter_cfg = cfg.get("filter", cfg.get("match", None))

    for n in notes:
        if n.fake:
            continue

        # Check if note matches filter
        should_scale = True
        if isinstance(filter_cfg, dict):
            should_scale = match_note_filter(n, filter_cfg)

        if not should_scale:
            continue

        # Apply scaling
        if size_mul != 1.0:
            n.size_px = float(n.size_px) * size_mul

        if speed_mul != 1.0:
            n.speed_mul = float(n.speed_mul) * speed_mul

        if x_mul != 1.0:
            # Scale x position from center
            offset_x = float(n.x_local_px) - float(x_center)
            n.x_local_px = float(x_center) + offset_x * x_mul

        if y_mul != 1.0:
            # Scale y position from center
            offset_y = float(n.y_offset_px) - float(y_center)
            n.y_offset_px = float(y_center) + offset_y * y_mul

    return notes
