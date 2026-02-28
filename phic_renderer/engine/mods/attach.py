from __future__ import annotations

from typing import Any, Dict, List, Optional

from ...types import RuntimeLine, RuntimeNote
from .base import match_note_filter, parse_kind, parse_float, apply_note_side


def apply_attach(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    """Attach mode: add an offset note to each existing note.

    Example: add a flick note to the right of each tap note.

    Config:
        attach:
            enable: true
            kind: 4  # Type of attached note (1=tap, 2=drag, 3=hold, 4=flick)
            x_offset: 100  # Horizontal offset in px (positive = right, negative = left)
            y_offset: 0    # Vertical offset in px
            time_offset: 0.0  # Time offset in seconds
            above: null  # Side of attached note (null=same as original, true=above, false=below, "flip"=opposite)
            filter:  # Optional: only attach to matching notes
                kinds: [1]  # Only attach to tap notes
                line_ids: [0, 1]  # Only on certain lines
    """
    cfg = None
    for k in ("attach", "attach_note", "add_note"):
        if k in mods_cfg:
            cfg = mods_cfg.get(k)
            break

    if not (isinstance(cfg, dict) and bool(cfg.get("enable", True))):
        return notes

    # Parse configuration
    try:
        attach_kind = parse_kind(cfg.get("kind", cfg.get("attach_kind", 4)))
        if attach_kind is None:
            attach_kind = 4  # Default to flick
    except Exception:
        attach_kind = 4

    try:
        x_offset = parse_float(cfg.get("x_offset", cfg.get("offset_x", 100)))
        if x_offset is None:
            x_offset = 100.0
    except Exception:
        x_offset = 100.0

    try:
        y_offset = parse_float(cfg.get("y_offset", cfg.get("offset_y", 0)))
        if y_offset is None:
            y_offset = 0.0
    except Exception:
        y_offset = 0.0

    try:
        time_offset = parse_float(cfg.get("time_offset", cfg.get("t_offset", 0)))
        if time_offset is None:
            time_offset = 0.0
    except Exception:
        time_offset = 0.0

    side_cfg = cfg.get("above", cfg.get("side", None))

    filter_cfg = cfg.get("filter", cfg.get("match", None))
    nid_next = max((int(n.nid) for n in notes), default=0) + 1
    line_map = {int(ln.lid): ln for ln in lines}
    out_notes: List[RuntimeNote] = []

    for n in notes:
        # Always keep original note
        out_notes.append(n)

        # Check if should attach to this note
        if n.fake:
            continue

        should_attach = True
        if isinstance(filter_cfg, dict):
            should_attach = match_note_filter(n, filter_cfg)

        if not should_attach:
            continue

        # Determine side of attached note
        if side_cfg is None:
            attached_above = bool(n.above)
        elif side_cfg == "flip" or side_cfg == "toggle" or side_cfg == "invert":
            attached_above = not bool(n.above)
        else:
            try:
                s = str(side_cfg).strip().lower()
                if s in {"above", "up", "top", "1", "true"}:
                    attached_above = True
                elif s in {"below", "down", "bottom", "0", "false"}:
                    attached_above = False
                else:
                    attached_above = bool(n.above)
            except Exception:
                attached_above = bool(n.above)

        # Create attached note
        attached = RuntimeNote(
            nid=int(nid_next),
            line_id=int(n.line_id),
            kind=int(attach_kind),
            above=bool(attached_above),
            fake=False,
            t_hit=float(n.t_hit) + float(time_offset),
            t_end=float(n.t_end) + float(time_offset) if attach_kind == 3 else float(n.t_hit) + float(time_offset),
            x_local_px=float(n.x_local_px) + float(x_offset),
            y_offset_px=float(n.y_offset_px) + float(y_offset),
            speed_mul=float(n.speed_mul),
            size_px=float(n.size_px),
            alpha01=float(n.alpha01),
            hitsound_path=n.hitsound_path,
            t_enter=-1e9,
            mh=False,
        )

        # Calculate scroll values
        ln = line_map.get(int(n.line_id))
        if ln is not None:
            attached.scroll_hit = ln.scroll_px.integral(attached.t_hit)
            attached.scroll_end = ln.scroll_px.integral(attached.t_end)
        else:
            attached.scroll_hit = n.scroll_hit
            attached.scroll_end = n.scroll_end

        # Copy optional attributes
        if hasattr(n, "tint_rgb"):
            try:
                attached.tint_rgb = n.tint_rgb
            except Exception:
                pass
        if hasattr(n, "tint_hitfx_rgb"):
            try:
                attached.tint_hitfx_rgb = n.tint_hitfx_rgb
            except Exception:
                pass

        out_notes.append(attached)
        nid_next += 1

    return sorted(out_notes, key=lambda x: x.t_hit)
