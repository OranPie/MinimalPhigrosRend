from __future__ import annotations

from typing import Any, Dict, List, Optional

from ...types import RuntimeNote
from ...io.chart_loader_impl import load_chart
from ...io.chart_pack_impl import load_chart_pack


def filter_notes_by_time(
    notes: List[RuntimeNote],
    start_time: Optional[float],
    end_time: Optional[float]
) -> List[RuntimeNote]:
    """Filter notes to only include those within the specified time range."""
    if start_time is None and end_time is None:
        return notes
    
    st = start_time if start_time is not None else -float("inf")
    et = end_time if end_time is not None else float("inf")
    
    filtered_notes = []
    for n in notes:
        if n.fake:
            continue
        if n.kind == 3:
            if n.t_end < st or n.t_hit > et:
                continue
        else:
            if n.t_hit < st or n.t_hit > et:
                continue
        filtered_notes.append(n)
    return filtered_notes


def group_simultaneous_notes(notes: List[RuntimeNote], eps: float = 1e-4):
    """Mark notes that hit at the same time as multi-hit (mh)."""
    i = 0
    while i < len(notes):
        j = i + 1
        while j < len(notes) and abs(notes[j].t_hit - notes[i].t_hit) <= eps:
            j += 1
        if (j - i) >= 2:
            for k in range(i, j):
                notes[k].mh = True
        i = j


def compute_total_notes(
    notes: List[RuntimeNote],
    advance_active: bool,
    advance_cfg: Optional[Dict[str, Any]],
    W: int,
    H: int
) -> int:
    """Compute total playable note count, handling advance composite mode."""
    if advance_active and advance_cfg and advance_cfg.get("mode") == "composite":
        unique_notes = set()
        tracks = advance_cfg.get("tracks", [])
        for track in tracks:
            inp = str(track.get("input"))
            import os
            if os.path.isdir(inp) or (os.path.isfile(inp) and str(inp).lower().endswith((".zip", ".pez"))):
                p = load_chart_pack(inp)
                chart_p = p.chart_path
            else:
                chart_p = inp
            _fmt_i, _off_i, _lines_i, notes_i = load_chart(chart_p, W, H)
            for n in notes_i:
                if not n.fake:
                    unique_notes.add((n.nid, n.t_hit, n.line_id))
        return len(unique_notes)
    else:
        return sum(1 for n in notes if not n.fake)


def compute_chart_end(
    notes: List[RuntimeNote],
    advance_active: bool,
    end_time: Optional[float]
) -> float:
    """Compute the end time of the chart."""
    if (not advance_active) and end_time is not None:
        return min(float(end_time), max((n.t_end for n in notes if not n.fake), default=float(end_time)))
    else:
        return max((n.t_end for n in notes if not n.fake), default=0.0)
