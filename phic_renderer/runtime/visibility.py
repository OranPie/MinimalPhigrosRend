from __future__ import annotations

from typing import List, Tuple, Optional

from ..types import RuntimeLine, RuntimeNote
from .kinematics import eval_line_state, note_world_pos
from .. import state

def _note_visible_on_screen(lines: List[RuntimeLine], note: RuntimeNote, t: float, W: int, H: int,
                            margin: int = 120, base_w: int = 80, base_h: int = 24) -> bool:
    expand_factor = state.expand_factor  # legacy alias
    ln = lines[note.line_id]
    lx, ly, lr, la, sc, _la_raw = eval_line_state(ln, t)
    x, y = note_world_pos(lx, ly, lr, sc, note, note.scroll_hit, for_tail=False)
    # approximate bounding box check (rotation doesn't matter, we use margin)
    ex = max(1.0, float(expand_factor))
    sx = float(getattr(state, "note_scale_x", 1.0) or 1.0) / ex
    sy = float(getattr(state, "note_scale_y", 1.0) or 1.0) / ex
    w = base_w * float(note.size_px) * sx
    h = base_h * float(note.size_px) * sy
    # expanded view: visible region in world coords extends beyond screen
    cx = W * 0.5
    cy = H * 0.5
    half_w = W * ex * 0.5
    half_h = H * ex * 0.5
    left = cx - half_w
    right = cx + half_w
    top = cy - half_h
    bottom = cy + half_h
    return (x + w/2 >= left - margin and x - w/2 <= right + margin and
            y + h/2 >= top - margin and y - h/2 <= bottom + margin)

def precompute_t_enter(lines: List[RuntimeLine], notes: List[RuntimeNote], W: int, H: int,
                       lookback_default: float = 8.0, dt: float = 1/30.0):
    """
    From t_hit scan backwards: find "invisible -> visible" boundary, then binary search to refine.
    """
    base_w = int(0.06 * W)
    base_h = int(0.018 * H)

    for n in notes:
        # RPE has visibleTime concept, but this renderer doesn't necessarily have it; use upper bound lookback
        t_hit = n.t_hit
        lookback = lookback_default
        t = t_hit
        earliest_visible = None
        was_visible = False

        steps = int(lookback / dt)
        for _ in range(steps):
            vis = _note_visible_on_screen(lines, n, t, W, H, base_w=base_w, base_h=base_h)
            if vis:
                earliest_visible = t
                was_visible = True
            elif was_visible:
                # just crossed from visible to invisible (backwards scan), earliest_visible is "earliest coarse location"
                lo = t
                hi = earliest_visible
                for _ in range(18):  # binary refine
                    mid = (lo + hi) * 0.5
                    if _note_visible_on_screen(lines, n, mid, W, H, base_w=base_w, base_h=base_h):
                        hi = mid
                    else:
                        lo = mid
                n.t_enter = hi
                break
            t -= dt

        if n.t_enter == -1e9:
            if was_visible:
                n.t_enter = -1e9
            else:
                n.t_enter = t_hit - lookback


