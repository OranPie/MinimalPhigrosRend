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

def _note_visible_on_screen(
    lines: List[RuntimeLine],
    note: RuntimeNote,
    t: float,
    W: int,
    H: int,
    margin: int = 120,
    base_w: int = 80,
    base_h: int = 24,
    expand_factor: float = 1.0,
    note_scale_x: float = 1.0,
    note_scale_y: float = 1.0,
) -> bool:
    """Check if note is visible on screen at time t.

    Args:
        lines: Judgment lines
        note: Note to check
        t: Current time
        W: Screen width
        H: Screen height
        margin: Screen margin for visibility check
        base_w: Base note width
        base_h: Base note height
        expand_factor: Camera expand factor
        note_scale_x: Note X scale
        note_scale_y: Note Y scale

    Returns:
        True if note is visible on screen
    """
    ln = lines[note.line_id]
    lx, ly, lr, la, sc, _la_raw = eval_line_state(ln, t)
    x, y = note_world_pos(lx, ly, lr, sc, note, note.scroll_hit, for_tail=False)
    # approximate bounding box check (rotation doesn't matter, we use margin)
    ex = max(1.0, float(expand_factor))
    sx = float(note_scale_x or 1.0) / ex
    sy = float(note_scale_y or 1.0) / ex
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

    dt0 = max(1e-4, float(dt))
    max_expand_iters = 32

    for n in notes:
        if getattr(n, "fake", False):
            n.t_enter = -1e9
            continue

        t_hit = float(n.t_hit)
        lookback = float(lookback_default)

        ln = None
        v = None
        try:
            ln = lines[n.line_id]
            v = _scroll_speed_px_per_sec(getattr(ln, "scroll_px", None), float(t_hit))
        except:
            ln = None
            v = None

        # If the line is essentially not scrolling, entry time can be extremely early / ill-defined.
        # Be conservative and avoid expensive scanning.
        try:
            if v is not None and float(v) <= 1e-4:
                n.t_enter = -1e9
                continue
        except:
            pass

        # Find a time point where the note is visible; prefer t_hit.
        t_vis = t_hit
        vis_at_hit = _note_visible_on_screen(lines, n, t_vis, W, H, base_w=base_w, base_h=base_h)
        if not vis_at_hit:
            step = dt0
            found = False
            for _ in range(max_expand_iters):
                t2 = float(t_hit) - float(step)
                if t2 < float(t_hit) - float(lookback):
                    break
                if _note_visible_on_screen(lines, n, t2, W, H, base_w=base_w, base_h=base_h):
                    t_vis = t2
                    found = True
                    break
                step *= 2.0
            if not found:
                # If we can't find visibility quickly, prefer earlier rendering (avoid pop-in).
                n.t_enter = float(t_hit) - float(lookback)
                continue

        # Exponential search backwards from a visible point until we find an invisible point.
        hi = float(t_vis)  # visible
        lo = None          # invisible
        step = dt0
        for _ in range(max_expand_iters):
            t2 = float(hi) - float(step)
            if t2 < float(t_hit) - float(lookback):
                break
            if _note_visible_on_screen(lines, n, t2, W, H, base_w=base_w, base_h=base_h):
                hi = t2
                step *= 2.0
            else:
                lo = t2
                break

        if lo is None:
            # Still visible all the way to the lookback bound; keep conservative.
            n.t_enter = float(t_hit) - float(lookback)
            continue

        # Binary search boundary (lo invisible, hi visible)
        for _ in range(20):
            mid = (float(lo) + float(hi)) * 0.5
            if _note_visible_on_screen(lines, n, mid, W, H, base_w=base_w, base_h=base_h):
                hi = mid
            else:
                lo = mid
        n.t_enter = float(hi)



