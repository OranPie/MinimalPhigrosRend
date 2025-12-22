from __future__ import annotations

from collections import deque
from typing import Any, Deque, Dict, List, Optional, Tuple

from ...math.util import apply_expand_xy
from .draw import draw_line_rgba, draw_ring


def draw_debug_pointer(
    *,
    display_frame: Any,
    args: Any,
    W: int,
    H: int,
    RW: int,
    RH: int,
    overrender: float,
    expand: float,
    pointers: Any,
    small: Any,
    hist: Dict[int, Deque[Tuple[float, float, int]]],
    now_ms: int,
):
    if not bool(getattr(args, "debug_pointer", False)):
        return

    # Keep trails for a limited time even after release.
    max_keep_ms = 1200

    try:
        frames = pointers.frame_pointers()
    except Exception:
        frames = []

    for pf in list(frames):
        try:
            pid = int(getattr(pf, "pointer_id", 0))
        except Exception:
            pid = 0
        try:
            x = getattr(pf, "x", None)
            y = getattr(pf, "y", None)
            if x is None or y is None:
                continue
            x = float(x)
            y = float(y)
        except Exception:
            continue

        dq = hist.get(int(pid))
        if dq is None:
            dq = deque(maxlen=64)
            hist[int(pid)] = dq
        dq.append((float(x), float(y), int(now_ms)))

    # Prune old points so released pointers fade out and then disappear.
    for pid, dq in list(hist.items()):
        if not dq:
            continue
        try:
            while dq and (int(now_ms) - int(dq[0][2])) > int(max_keep_ms):
                dq.popleft()
        except Exception:
            pass
        if not dq:
            try:
                hist.pop(int(pid), None)
            except Exception:
                pass

    for pid, dq in list(hist.items()):
        if not dq:
            continue
        pts: List[Tuple[float, float, int]] = list(dq)
        if len(pts) >= 2:
            for i in range(1, len(pts)):
                x0, y0, t0 = pts[i - 1]
                x1, y1, t1 = pts[i]
                age_ms = int(now_ms) - int(t1)
                if age_ms > int(max_keep_ms):
                    continue
                a = max(10, 190 - int(age_ms * 0.16))
                p0 = apply_expand_xy(float(x0) * float(overrender), float(y0) * float(overrender), int(RW), int(RH), float(expand))
                p1 = apply_expand_xy(float(x1) * float(overrender), float(y1) * float(overrender), int(RW), int(RH), float(expand))
                draw_line_rgba(display_frame, p0, p1, (220, 220, 220, int(a)), width=max(1, int(2 * overrender)))

    for pf in list(frames):
        try:
            pid = int(getattr(pf, "pointer_id", 0))
        except Exception:
            pid = 0
        x = getattr(pf, "x", None)
        y = getattr(pf, "y", None)
        if x is None or y is None:
            continue
        try:
            x = float(x)
            y = float(y)
        except Exception:
            continue

        down = bool(getattr(pf, "down", False))
        press_edge = bool(getattr(pf, "press_edge", False))
        release_edge = bool(getattr(pf, "release_edge", False))
        gesture = getattr(pf, "gesture", None)

        start_x = getattr(pf, "start_x", None)
        start_y = getattr(pf, "start_y", None)
        moved_y = getattr(pf, "moved_y", 0.0)
        try:
            moved_y = float(moved_y)
        except Exception:
            moved_y = 0.0

        col = (80, 220, 120, 190) if down else (220, 220, 220, 140)
        if press_edge:
            col = (80, 220, 255, 220)
        if release_edge:
            col = (255, 120, 80, 220)

        ps = apply_expand_xy(float(x) * float(overrender), float(y) * float(overrender), int(RW), int(RH), float(expand))
        draw_ring(
            display_frame,
            float(ps[0]),
            float(ps[1]),
            int(9 * overrender),
            col,
            thickness=max(1, int(3 * overrender)),
        )

        # Vertical flick judgement visualization.
        # - Draw a line from start_y to current y, and threshold ticks.
        if down and (start_x is not None) and (start_y is not None):
            try:
                thr_ratio = float(getattr(pointers, "flick_threshold_ratio", 0.02) or 0.02)
            except Exception:
                thr_ratio = 0.02
            thr_px = float(thr_ratio) * float(min(int(W), int(H)))
            try:
                sx = float(start_x)
                sy = float(start_y)
            except Exception:
                sx = sy = None
            if sx is not None and sy is not None:
                p0 = apply_expand_xy(float(sx) * float(overrender), float(sy) * float(overrender), int(RW), int(RH), float(expand))
                # current is ps
                ok = bool(float(moved_y) >= float(thr_px))
                col_v = (120, 255, 160, 210) if ok else (255, 200, 80, 210)
                draw_line_rgba(display_frame, p0, ps, col_v, width=max(1, int(2 * overrender)))

                # threshold markers: sy +/- thr
                for sign in (-1.0, 1.0):
                    ty = float(sy) + float(sign) * float(thr_px)
                    p_thr = apply_expand_xy(float(sx) * float(overrender), float(ty) * float(overrender), int(RW), int(RH), float(expand))
                    draw_ring(
                        display_frame,
                        float(p_thr[0]),
                        float(p_thr[1]),
                        int(5 * overrender),
                        (200, 200, 200, 120),
                        thickness=max(1, int(2 * overrender)),
                    )

        try:
            label = f"P{int(pid)}"
            if gesture is not None:
                label += f" {str(gesture)}"
            if down:
                try:
                    thr_ratio = float(getattr(pointers, "flick_threshold_ratio", 0.02) or 0.02)
                except Exception:
                    thr_ratio = 0.02
                thr_px = float(thr_ratio) * float(min(int(W), int(H)))
                label += f" dy={moved_y:.1f}/{thr_px:.1f}"
            txt = small.render(label, True, (240, 240, 240))
            display_frame.blit(txt, (int(ps[0] + 6 * overrender), int(ps[1] + 6 * overrender)))
        except Exception:
            pass
