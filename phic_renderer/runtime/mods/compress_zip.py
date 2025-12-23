from __future__ import annotations

from typing import Any, Dict, List

from ...types import RuntimeLine, RuntimeNote
from .base import match_note_filter, parse_int


def apply_compress_zip(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    """Compress/zip mode: duplicate each note N times at the same position.

    Example: make a single note become 20 identical notes.

    Config:
        compress_zip:
            enable: true
            count: 20  # Number of duplicates per note (default: 2)
            filter:    # Optional: only apply to matching notes
                kinds: [1, 2]  # Only duplicate tap and drag
    """
    cfg = None
    for k in ("compress_zip", "compress", "zip", "duplicate"):
        if k in mods_cfg:
            cfg = mods_cfg.get(k)
            break

    if not (isinstance(cfg, dict) and bool(cfg.get("enable", True))):
        return notes

    try:
        count = max(1, int(cfg.get("count", cfg.get("duplicates", 2))))
    except Exception:
        count = 2

    # If count is 1, no duplication needed
    if count <= 1:
        return notes

    filter_cfg = cfg.get("filter", cfg.get("match", None))
    nid_next = max((int(n.nid) for n in notes), default=0) + 1
    line_map = {int(ln.lid): ln for ln in lines}
    out_notes: List[RuntimeNote] = []

    for n in notes:
        # Check if note matches filter
        should_duplicate = True
        if isinstance(filter_cfg, dict):
            should_duplicate = match_note_filter(n, filter_cfg)

        if not should_duplicate or n.fake:
            out_notes.append(n)
            continue

        # Keep original note
        out_notes.append(n)

        # Create duplicates
        for i in range(count - 1):
            dup = RuntimeNote(
                nid=int(nid_next),
                line_id=int(n.line_id),
                kind=int(n.kind),
                above=bool(n.above),
                fake=False,
                t_hit=float(n.t_hit),
                t_end=float(n.t_end),
                x_local_px=float(n.x_local_px),
                y_offset_px=float(n.y_offset_px),
                speed_mul=float(n.speed_mul),
                size_px=float(n.size_px),
                alpha01=float(n.alpha01),
                hitsound_path=n.hitsound_path,
                t_enter=-1e9,
                mh=False,
            )

            # Copy scroll values
            ln = line_map.get(int(n.line_id))
            if ln is not None:
                dup.scroll_hit = ln.scroll_px.integral(dup.t_hit)
                dup.scroll_end = ln.scroll_px.integral(dup.t_end)
            else:
                dup.scroll_hit = n.scroll_hit
                dup.scroll_end = n.scroll_end

            # Copy optional attributes
            if hasattr(n, "tint_rgb"):
                try:
                    dup.tint_rgb = n.tint_rgb
                except Exception:
                    pass
            if hasattr(n, "tint_hitfx_rgb"):
                try:
                    dup.tint_hitfx_rgb = n.tint_hitfx_rgb
                except Exception:
                    pass

            out_notes.append(dup)
            nid_next += 1

    return sorted(out_notes, key=lambda x: x.t_hit)
