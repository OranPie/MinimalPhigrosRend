from __future__ import annotations

from typing import Any, Dict, List

from ...types import RuntimeLine, RuntimeNote
from .base import match_note_filter, parse_float


def apply_stutter(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    """Stutter mode: create stutter/echo effects by repeating notes.

    Creates rhythmic repetitions of notes with customizable delay, count, and decay.
    Each repetition can have modified properties (position, alpha, size).

    Config:
        stutter:
            enable: true
            count: 3  # Number of repetitions (including original)
            delay: 0.05  # Time delay between repetitions (seconds)
            x_offset: 20  # X position offset per repetition
            y_offset: 0  # Y position offset per repetition
            alpha_decay: 0.8  # Alpha multiplier per repetition (1.0 = no decay)
            size_decay: 0.9  # Size multiplier per repetition (1.0 = no decay)
            filter:  # Optional: only stutter matching notes
                kinds: [1, 4]
    """
    cfg = None
    for k in ("stutter", "echo", "repeat"):
        if k in mods_cfg:
            cfg = mods_cfg.get(k)
            break

    if not (isinstance(cfg, dict) and bool(cfg.get("enable", True))):
        return notes

    # Parse parameters
    try:
        count = max(1, int(cfg.get("count", cfg.get("repetitions", 3))))
    except Exception:
        count = 3

    if count <= 1:
        return notes

    try:
        delay = float(cfg.get("delay", cfg.get("time_offset", 0.05)))
    except Exception:
        delay = 0.05

    try:
        x_offset = float(cfg.get("x_offset", 20))
    except Exception:
        x_offset = 20.0

    try:
        y_offset = float(cfg.get("y_offset", 0))
    except Exception:
        y_offset = 0.0

    try:
        alpha_decay = float(cfg.get("alpha_decay", cfg.get("opacity_decay", 0.8)))
    except Exception:
        alpha_decay = 0.8

    try:
        size_decay = float(cfg.get("size_decay", 0.9))
    except Exception:
        size_decay = 0.9

    filter_cfg = cfg.get("filter", cfg.get("match", None))
    nid_next = max((int(n.nid) for n in notes), default=0) + 1
    line_map = {int(ln.lid): ln for ln in lines}
    out_notes: List[RuntimeNote] = []

    for n in notes:
        # Always keep original note
        out_notes.append(n)

        # Check if should stutter this note
        should_stutter = True
        if n.fake:
            should_stutter = False
        elif isinstance(filter_cfg, dict):
            should_stutter = match_note_filter(n, filter_cfg)

        if not should_stutter:
            continue

        # Create repetitions
        for rep in range(1, count):
            # Calculate decayed properties
            alpha_mul = alpha_decay ** rep
            size_mul = size_decay ** rep

            # Create repeated note
            repeated = RuntimeNote(
                nid=int(nid_next),
                line_id=int(n.line_id),
                kind=int(n.kind),
                above=bool(n.above),
                fake=False,
                t_hit=float(n.t_hit) + float(delay) * rep,
                t_end=float(n.t_end) + float(delay) * rep if int(n.kind) == 3 else float(n.t_hit) + float(delay) * rep,
                x_local_px=float(n.x_local_px) + float(x_offset) * rep,
                y_offset_px=float(n.y_offset_px) + float(y_offset) * rep,
                speed_mul=float(n.speed_mul),
                size_px=float(n.size_px) * size_mul,
                alpha01=max(0.0, min(1.0, float(n.alpha01) * alpha_mul)),
                hitsound_path=n.hitsound_path,
                t_enter=-1e9,
                mh=False,
            )

            # Calculate scroll values
            ln = line_map.get(int(n.line_id))
            if ln is not None:
                repeated.scroll_hit = ln.scroll_px.integral(repeated.t_hit)
                repeated.scroll_end = ln.scroll_px.integral(repeated.t_end)
            else:
                repeated.scroll_hit = n.scroll_hit
                repeated.scroll_end = n.scroll_end

            # Copy optional attributes
            if hasattr(n, "tint_rgb"):
                try:
                    repeated.tint_rgb = n.tint_rgb
                except Exception:
                    pass
            if hasattr(n, "tint_hitfx_rgb"):
                try:
                    repeated.tint_hitfx_rgb = n.tint_hitfx_rgb
                except Exception:
                    pass

            out_notes.append(repeated)
            nid_next += 1

    return sorted(out_notes, key=lambda x: x.t_hit)
