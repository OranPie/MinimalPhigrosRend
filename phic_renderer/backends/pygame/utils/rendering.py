from __future__ import annotations

import bisect
from typing import Any, Dict, List, Optional, Tuple

import pygame

from ....types import RuntimeNote


def pick_note_image(note: RuntimeNote, respack: Any) -> Optional[pygame.Surface]:
    """Select the appropriate note texture from respack based on note type and mh flag."""
    if not respack:
        return None
    if note.kind == 1:
        return respack.img["click_mh.png"] if note.mh else respack.img["click.png"]
    if note.kind == 2:
        return respack.img["drag_mh.png"] if note.mh else respack.img["drag.png"]
    if note.kind == 3:
        return respack.img["hold_mh.png"] if note.mh else respack.img["hold.png"]
    if note.kind == 4:
        return respack.img["flick_mh.png"] if note.mh else respack.img["flick.png"]
    return respack.img["click_mh.png"] if note.mh else respack.img["click.png"]


def compute_note_times_by_line(notes: List[RuntimeNote]) -> Dict[int, List[float]]:
    """Build a mapping of line_id -> sorted list of note hit times."""
    note_times_by_line: Dict[int, List[float]] = {}
    for n in notes:
        if n.fake:
            continue
        note_times_by_line.setdefault(n.line_id, []).append(n.t_hit)
    for k in note_times_by_line:
        note_times_by_line[k].sort()
    return note_times_by_line


def compute_note_times_by_line_kind(notes: List[RuntimeNote]) -> Tuple[Dict[int, Dict[int, List[float]]], Dict[int, List[float]]]:
    """Build mappings for note times by line+kind and by kind only."""
    note_times_by_line_kind: Dict[int, Dict[int, List[float]]] = {}
    note_times_by_kind: Dict[int, List[float]] = {}
    for n in notes:
        if n.fake:
            continue
        lid = int(n.line_id)
        kd = int(n.kind)
        note_times_by_line_kind.setdefault(lid, {}).setdefault(kd, []).append(float(n.t_hit))
        note_times_by_kind.setdefault(kd, []).append(float(n.t_hit))
    for lid in note_times_by_line_kind:
        for kd in note_times_by_line_kind[lid]:
            note_times_by_line_kind[lid][kd].sort()
    for kd in note_times_by_kind:
        note_times_by_kind[kd].sort()
    return note_times_by_line_kind, note_times_by_kind


def line_note_counts(note_times_by_line: Dict[int, List[float]], lid: int, t: float, approach: float) -> Tuple[int, int]:
    """Count past and incoming notes for a specific line."""
    arr = note_times_by_line.get(lid, [])
    past = bisect.bisect_left(arr, t)
    incoming = bisect.bisect_right(arr, t + approach) - past
    return past, incoming


def line_note_counts_kind(
    note_times_by_line_kind: Dict[int, Dict[int, List[float]]],
    lid: int,
    t: float,
    approach: float
) -> Tuple[List[int], List[int]]:
    """Count past and incoming notes by kind for a specific line."""
    byk = note_times_by_line_kind.get(lid, {})
    past4 = [0, 0, 0, 0]
    inc4 = [0, 0, 0, 0]
    t1 = float(t) + float(approach)
    for kd, arr in byk.items():
        idx = int(kd) - 1
        if idx < 0 or idx >= 4:
            continue
        p = bisect.bisect_left(arr, t)
        q = bisect.bisect_right(arr, t1) - p
        past4[idx] = int(p)
        inc4[idx] = int(q)
    return past4, inc4


def track_seg_state(tr: Any) -> str:
    """Get segment state string for a track (for debug display)."""
    if hasattr(tr, "segs") and isinstance(getattr(tr, "segs"), list):
        total = len(tr.segs)
        idx = getattr(tr, "i", 0)
        return f"{idx}/{max(0, total - 1)}"
    return "-"


def scroll_speed_px_per_sec(ln: Any, t: float) -> float:
    """Calculate scroll speed in pixels per second at time t."""
    dt = 0.01
    a = ln.scroll_px.integral(t - dt)
    b = ln.scroll_px.integral(t + dt)
    return (b - a) / (2 * dt)
