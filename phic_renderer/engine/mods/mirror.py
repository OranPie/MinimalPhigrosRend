from __future__ import annotations

from typing import Any, Dict, List

from ...types import RuntimeLine, RuntimeNote
from .base import match_note_filter


def apply_mirror(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    """Mirror mode: flip notes horizontally (or vertically).

    Example: mirror all notes to create left-right symmetry.

    Config:
        mirror:
            enable: true
            axis: "x"  # "x" for horizontal flip, "y" for vertical flip (default: "x")
            center: 0  # Center point for mirroring (default: 0 for x_local_px)
            flip_side: true  # Also flip above/below side (default: true for x-axis)
            filter:  # Optional: only mirror matching notes
                kinds: [1, 2]  # Only mirror tap and drag
    """
    cfg = None
    for k in ("mirror", "flip", "reflect"):
        if k in mods_cfg:
            cfg = mods_cfg.get(k)
            break

    if not (isinstance(cfg, dict) and bool(cfg.get("enable", True))):
        return notes

    # Parse configuration
    axis = str(cfg.get("axis", "x")).strip().lower()
    if axis not in ("x", "y", "horizontal", "vertical", "h", "v"):
        axis = "x"
    if axis in ("horizontal", "h"):
        axis = "x"
    if axis in ("vertical", "v"):
        axis = "y"

    try:
        center = float(cfg.get("center", 0))
    except Exception:
        center = 0.0

    flip_side = bool(cfg.get("flip_side", axis == "x"))
    filter_cfg = cfg.get("filter", cfg.get("match", None))

    for n in notes:
        if n.fake:
            continue

        # Check if note matches filter
        should_mirror = True
        if isinstance(filter_cfg, dict):
            should_mirror = match_note_filter(n, filter_cfg)

        if not should_mirror:
            continue

        # Apply mirror transformation
        if axis == "x":
            # Flip horizontal: x_local_px around center
            n.x_local_px = float(center) - (float(n.x_local_px) - float(center))
            if flip_side:
                n.above = not bool(n.above)
        elif axis == "y":
            # Flip vertical: y_offset_px around center
            n.y_offset_px = float(center) - (float(n.y_offset_px) - float(center))
            # Don't flip side for vertical mirror by default

    return notes
