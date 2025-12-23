from __future__ import annotations

from typing import Any, Dict, List, Tuple

from ...types import RuntimeLine, RuntimeNote
from .base import match_note_filter, parse_rgb


def apply_colorize(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    """Colorize mode: apply color tints to notes.

    Applies RGB color tints to notes and/or hit effects. Supports gradient effects
    based on time progression.

    Config:
        colorize:
            enable: true
            mode: "constant"  # "constant", "gradient", "by_kind", "by_line"
            tint: "#FF00FF"  # Note tint color (constant mode)
            tint_hitfx: "#FF00FF"  # Hit effect tint color
            gradient_start: "#FF0000"  # Gradient start color (gradient mode)
            gradient_end: "#0000FF"  # Gradient end color (gradient mode)
            by_kind:  # Color mapping by note kind (by_kind mode)
              "1": "#FF0000"  # Tap = red
              "2": "#00FF00"  # Drag = green
              "3": "#0000FF"  # Hold = blue
              "4": "#FFFF00"  # Flick = yellow
            by_line:  # Color mapping by line ID (by_line mode)
              "0": "#FF0000"
              "1": "#00FF00"
            filter:  # Optional: only colorize matching notes
                kinds: [1, 2, 4]
    """
    cfg = None
    for k in ("colorize", "tint", "color", "paint"):
        if k in mods_cfg:
            cfg = mods_cfg.get(k)
            break

    if not (isinstance(cfg, dict) and bool(cfg.get("enable", True))):
        return notes

    # Parse mode
    mode = str(cfg.get("mode", "constant")).strip().lower()

    # Parse constant colors
    tint = parse_rgb(cfg.get("tint", cfg.get("color", None)))
    tint_hitfx = parse_rgb(cfg.get("tint_hitfx", cfg.get("hitfx_color", None)))

    # Parse gradient colors
    gradient_start = parse_rgb(cfg.get("gradient_start", None))
    gradient_end = parse_rgb(cfg.get("gradient_end", None))

    # Parse by_kind mapping
    by_kind_map: Dict[int, Tuple[int, int, int]] = {}
    by_kind_cfg = cfg.get("by_kind", cfg.get("kind_colors", None))
    if isinstance(by_kind_cfg, dict):
        for k, v in by_kind_cfg.items():
            try:
                kind_i = int(k)
                rgb = parse_rgb(v)
                if rgb is not None:
                    by_kind_map[kind_i] = rgb
            except Exception:
                pass

    # Parse by_line mapping
    by_line_map: Dict[int, Tuple[int, int, int]] = {}
    by_line_cfg = cfg.get("by_line", cfg.get("line_colors", None))
    if isinstance(by_line_cfg, dict):
        for k, v in by_line_cfg.items():
            try:
                line_i = int(k)
                rgb = parse_rgb(v)
                if rgb is not None:
                    by_line_map[line_i] = rgb
            except Exception:
                pass

    filter_cfg = cfg.get("filter", cfg.get("match", None))

    # For gradient mode, find time range
    if mode == "gradient" and gradient_start is not None and gradient_end is not None:
        matching_notes = [n for n in notes if not n.fake]
        if isinstance(filter_cfg, dict):
            matching_notes = [n for n in matching_notes if match_note_filter(n, filter_cfg)]

        if matching_notes:
            try:
                min_t = min(float(n.t_hit) for n in matching_notes)
                max_t = max(float(n.t_hit) for n in matching_notes)
                time_range = max_t - min_t
            except Exception:
                min_t = 0.0
                time_range = 1.0
        else:
            min_t = 0.0
            time_range = 1.0
    else:
        min_t = 0.0
        time_range = 1.0

    def lerp_color(c1: Tuple[int, int, int], c2: Tuple[int, int, int], t: float) -> Tuple[int, int, int]:
        """Linear interpolation between two colors."""
        t = max(0.0, min(1.0, t))
        r = int(c1[0] + (c2[0] - c1[0]) * t)
        g = int(c1[1] + (c2[1] - c1[1]) * t)
        b = int(c1[2] + (c2[2] - c1[2]) * t)
        return (r, g, b)

    for n in notes:
        if n.fake:
            continue

        # Check if note matches filter
        should_colorize = True
        if isinstance(filter_cfg, dict):
            should_colorize = match_note_filter(n, filter_cfg)

        if not should_colorize:
            continue

        # Determine color based on mode
        note_color = None

        if mode == "constant":
            note_color = tint
        elif mode == "gradient":
            if gradient_start is not None and gradient_end is not None and time_range > 0:
                progress = (float(n.t_hit) - min_t) / time_range
                note_color = lerp_color(gradient_start, gradient_end, progress)
        elif mode == "by_kind":
            note_color = by_kind_map.get(int(n.kind), None)
        elif mode == "by_line":
            note_color = by_line_map.get(int(n.line_id), None)

        # Apply colors
        if note_color is not None:
            n.tint_rgb = note_color

        if tint_hitfx is not None:
            n.tint_hitfx_rgb = tint_hitfx

    return notes
