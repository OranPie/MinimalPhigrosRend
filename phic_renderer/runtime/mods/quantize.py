from __future__ import annotations

from typing import Any, Dict, List

from ...types import RuntimeLine, RuntimeNote
from .base import match_note_filter, parse_float


def apply_quantize(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    """Quantize mode: snap note properties to grid/steps.

    Example: snap all notes to 16th note grid, or quantize x positions to multiples of 50px.

    Config:
        quantize:
            enable: true
            time_grid: 0.125  # Snap time to multiples of this (e.g., 0.125 = 16th note at 120 BPM)
            x_grid: 50  # Snap x position to multiples of this (px)
            y_grid: 10  # Snap y position to multiples of this (px)
            size_grid: 5  # Snap size to multiples of this (px)
            filter:  # Optional: only quantize matching notes
                kinds: [1, 2, 4]
    """
    cfg = None
    for k in ("quantize", "snap", "grid"):
        if k in mods_cfg:
            cfg = mods_cfg.get(k)
            break

    if not (isinstance(cfg, dict) and bool(cfg.get("enable", True))):
        return notes

    # Parse grid sizes
    time_grid = parse_float(cfg.get("time_grid", cfg.get("time_step", None)))
    x_grid = parse_float(cfg.get("x_grid", cfg.get("x_step", None)))
    y_grid = parse_float(cfg.get("y_grid", cfg.get("y_step", None)))
    size_grid = parse_float(cfg.get("size_grid", cfg.get("size_step", None)))

    filter_cfg = cfg.get("filter", cfg.get("match", None))

    def snap_to_grid(value: float, grid: float) -> float:
        """Snap value to nearest multiple of grid."""
        if grid <= 0:
            return value
        return round(value / grid) * grid

    for n in notes:
        if n.fake:
            continue

        # Check if note matches filter
        should_quantize = True
        if isinstance(filter_cfg, dict):
            should_quantize = match_note_filter(n, filter_cfg)

        if not should_quantize:
            continue

        # Apply quantization
        if time_grid is not None and time_grid > 0:
            n.t_hit = snap_to_grid(float(n.t_hit), time_grid)
            # For holds, also quantize end time
            if int(n.kind) == 3:
                n.t_end = snap_to_grid(float(n.t_end), time_grid)

        if x_grid is not None and x_grid > 0:
            n.x_local_px = snap_to_grid(float(n.x_local_px), x_grid)

        if y_grid is not None and y_grid > 0:
            n.y_offset_px = snap_to_grid(float(n.y_offset_px), y_grid)

        if size_grid is not None and size_grid > 0:
            n.size_px = snap_to_grid(float(n.size_px), size_grid)

    # Re-sort by hit time since timing may have changed
    return sorted(notes, key=lambda x: x.t_hit)
