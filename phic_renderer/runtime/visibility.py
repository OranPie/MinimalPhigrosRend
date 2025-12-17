from __future__ import annotations

from typing import List, Tuple, Optional

from ..types import RuntimeLine, RuntimeNote
from .kinematics import eval_line_state, note_world_pos
from .. import state


def _scroll_speed_px_per_sec(scroll_track: object, t: float) -> Optional[float]:
    try:
        segs = getattr(scroll_track, "segs", None)
        if not segs:
            return None
        for s in segs:
            try:
                if float(t) < float(s.t0):
                    break
                if float(t) <= float(s.t1):
                    return abs(float(s.v0))
            except:
                continue
        try:
            last = segs[-1]
            return abs(float(getattr(last, "v1", getattr(last, "v0", 0.0))))
        except:
            return None
    except:
        return None

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
    # Be conservative: when in doubt, treat notes as visible earlier to avoid accidental culling.
    try:
        margin = max(int(margin), int(0.18 * max(W, H) * ex))
    except:
        margin = int(margin)
    return (x + w/2 >= left - margin and x - w/2 <= right + margin and
            y + h/2 >= top - margin and y - h/2 <= bottom + margin)

def precompute_t_enter(lines: List[RuntimeLine], notes: List[RuntimeNote], W: int, H: int,
                       lookback_default: float = 256.0, dt: float = 1/30.0):
    """
    From t_hit scan backwards: find "invisible -> visible" boundary, then binary search to refine.
    """
    base_w = int(0.06 * W)
    base_h = int(0.018 * H)

    for n in notes:
        # RPE has visibleTime concept, but this renderer doesn't necessarily have it; use upper bound lookback
        t_hit = n.t_hit
        lookback = float(lookback_default)
        try:
            if float(getattr(n, "speed_mul", 1.0)) == 0.0:
                lookback = max(float(lookback), 666.66)
        except:
            pass
        try:
            ln = lines[n.line_id]
            v = _scroll_speed_px_per_sec(getattr(ln, "scroll_px", None), float(t_hit))
            if v is not None and float(v) <= 1e-3:
                lookback = max(float(lookback), 666.66)
        except:
            pass

        dt0 = max(1e-4, float(dt))
        max_steps = 12000
        dt_scan = dt0
        try:
            if (float(lookback) / dt_scan) > float(max_steps):
                dt_scan = float(lookback) / float(max_steps)
        except:
            dt_scan = dt0

        t = float(t_hit)
        earliest_visible = None
        was_visible = False

        steps = int(float(lookback) / float(dt_scan))
        for _ in range(steps):
            vis = _note_visible_on_screen(lines, n, t, W, H, base_w=base_w, base_h=base_h)
            if vis:
                earliest_visible = t
                was_visible = True
            elif was_visible:
                lo = t
                hi = earliest_visible
                for _ in range(18):
                    mid = (lo + hi) * 0.5
                    if _note_visible_on_screen(lines, n, mid, W, H, base_w=base_w, base_h=base_h):
                        hi = mid
                    else:
                        lo = mid
                n.t_enter = hi
                break
            t -= float(dt_scan)

        if n.t_enter == -1e9:
            if was_visible:
                n.t_enter = -1e9
            else:
                n.t_enter = float(t_hit) - float(lookback)



