from __future__ import annotations

import math
from collections import deque
from typing import Any, Deque, Dict, List, Optional, Tuple

from ....math.util import apply_expand_xy
from ..rendering.draw import draw_line_rgba, draw_ring, draw_poly_rgba


def _pointer_color(pid: int) -> Tuple[int, int, int]:
    """Get unique color for each pointer ID"""
    colors = [
        (100, 200, 255),  # Cyan
        (255, 150, 100),  # Orange
        (150, 255, 150),  # Green
        (255, 150, 255),  # Magenta
        (255, 255, 100),  # Yellow
        (150, 150, 255),  # Purple
    ]
    return colors[int(pid) % len(colors)]


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

    # Keep trails for a longer time to show pointer movement history
    max_keep_ms = 2000

    try:
        frames = pointers.frame_pointers()
    except Exception:
        frames = []

    # Update history
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
            dq = deque(maxlen=120)  # Increased for smoother trails
            hist[int(pid)] = dq
        dq.append((float(x), float(y), int(now_ms)))

    # Prune old points
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

    # Draw trails with gradient fade
    for pid, dq in list(hist.items()):
        if not dq:
            continue
        pts: List[Tuple[float, float, int]] = list(dq)
        if len(pts) >= 2:
            base_color = _pointer_color(int(pid))
            for i in range(1, len(pts)):
                x0, y0, t0 = pts[i - 1]
                x1, y1, t1 = pts[i]
                age_ms = int(now_ms) - int(t1)
                if age_ms > int(max_keep_ms):
                    continue

                # Progressive width: wider at current position
                progress = float(i) / float(len(pts))
                width = max(1, int((1.0 + progress * 3.0) * overrender))

                # Fade based on age
                alpha_fade = max(20, 220 - int(age_ms * 0.11))

                # Position-based gradient: more opaque at tail
                alpha = int(float(alpha_fade) * (0.3 + 0.7 * progress))

                p0 = apply_expand_xy(float(x0) * float(overrender), float(y0) * float(overrender), int(RW), int(RH), float(expand))
                p1 = apply_expand_xy(float(x1) * float(overrender), float(y1) * float(overrender), int(RW), int(RH), float(expand))
                draw_line_rgba(display_frame, p0, p1, (*base_color, alpha), width=width)

    # Draw pointer indicators
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
        moved_px = getattr(pf, "moved_px", 0.0)
        moved_y = getattr(pf, "moved_y", 0.0)
        try:
            moved_px = float(moved_px)
            moved_y = float(moved_y)
        except Exception:
            moved_px = moved_y = 0.0

        ps = apply_expand_xy(float(x) * float(overrender), float(y) * float(overrender), int(RW), int(RH), float(expand))

        # Get pointer color
        base_color = _pointer_color(int(pid))

        # Outer glow (shadow effect)
        if down:
            for r_off in range(3, 0, -1):
                r = int((16 + r_off * 4) * overrender)
                alpha = max(10, 60 - r_off * 15)
                draw_ring(display_frame, float(ps[0]), float(ps[1]), r, (*base_color, alpha), thickness=max(1, int(2 * overrender)))

        # State-specific colors and sizes
        if press_edge:
            # Bright pulse on press
            col = (*base_color, 255)
            inner_r = int(14 * overrender)
            outer_r = int(20 * overrender)
        elif release_edge:
            # Fade on release
            col = (255, 200, 200, 180)
            inner_r = int(10 * overrender)
            outer_r = int(16 * overrender)
        elif down:
            # Solid while held
            col = (*base_color, 220)
            inner_r = int(12 * overrender)
            outer_r = int(18 * overrender)
        else:
            # Ghost when not down
            col = (*base_color, 100)
            inner_r = int(8 * overrender)
            outer_r = int(14 * overrender)

        # Draw pointer rings
        draw_ring(display_frame, float(ps[0]), float(ps[1]), outer_r, col, thickness=max(1, int(3 * overrender)))
        if down:
            # Inner filled circle
            draw_ring(display_frame, float(ps[0]), float(ps[1]), inner_r // 2, (*col[:3], col[3] // 2), thickness=inner_r // 2)

        # Draw movement vector if significant
        if down and moved_px > 5.0:
            try:
                dq = hist.get(int(pid))
                if dq and len(dq) >= 2:
                    # Get direction from recent movement
                    x_old, y_old, _ = dq[-min(10, len(dq))]
                    dx = float(x) - float(x_old)
                    dy = float(y) - float(y_old)
                    dist = math.sqrt(dx * dx + dy * dy)
                    if dist > 1.0:
                        # Normalize
                        dx /= dist
                        dy /= dist
                        # Draw arrow
                        arrow_len = min(40.0 * overrender, moved_px * 0.5)
                        ex = float(ps[0]) + dx * arrow_len
                        ey = float(ps[1]) + dy * arrow_len
                        draw_line_rgba(display_frame, ps, (ex, ey), (*base_color, 200), width=max(1, int(3 * overrender)))
                        # Arrow head
                        head_size = 8.0 * overrender
                        angle = math.atan2(dy, dx)
                        for side in [-1, 1]:
                            angle_head = angle + side * 2.8
                            hx = ex - math.cos(angle_head) * head_size
                            hy = ey - math.sin(angle_head) * head_size
                            draw_line_rgba(display_frame, (ex, ey), (hx, hy), (*base_color, 200), width=max(1, int(2 * overrender)))
            except Exception:
                pass

        # Flick visualization: line from start to current with threshold markers
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
                # Flick threshold met?
                flick_ok = bool(float(moved_y) >= float(thr_px))
                col_flick = (150, 255, 180, 180) if flick_ok else (255, 220, 100, 180)
                draw_line_rgba(display_frame, p0, ps, col_flick, width=max(1, int(2 * overrender)))

                # Draw threshold markers
                for sign in (-1.0, 1.0):
                    ty = float(sy) + float(sign) * float(thr_px)
                    p_thr = apply_expand_xy(float(sx) * float(overrender), float(ty) * float(overrender), int(RW), int(RH), float(expand))
                    marker_col = (180, 255, 200, 140) if flick_ok else (255, 240, 120, 140)
                    draw_ring(
                        display_frame,
                        float(p_thr[0]),
                        float(p_thr[1]),
                        int(6 * overrender),
                        marker_col,
                        thickness=max(1, int(2 * overrender)),
                    )

        # Information label with background
        try:
            lines_text = []
            # Line 1: Pointer ID and gesture
            label_1 = f"P{int(pid)}"
            if gesture is not None:
                label_1 += f" [{str(gesture).upper()}]"
            lines_text.append(label_1)

            # Line 2: Position
            lines_text.append(f"pos: ({x:.0f}, {y:.0f})")

            # Line 3: Movement info
            if down:
                try:
                    thr_ratio = float(getattr(pointers, "flick_threshold_ratio", 0.02) or 0.02)
                except Exception:
                    thr_ratio = 0.02
                thr_px = float(thr_ratio) * float(min(int(W), int(H)))
                lines_text.append(f"move: {moved_px:.1f}px")
                lines_text.append(f"vert: {moved_y:.1f}/{thr_px:.1f}px")

            # Render all lines
            y_offset = 0
            for line_text in lines_text:
                txt = small.render(line_text, True, (255, 255, 255))
                # Background box
                padding = int(3 * overrender)
                box_x = int(ps[0] + 16 * overrender)
                box_y = int(ps[1] + 6 * overrender + y_offset)
                box_w = txt.get_width() + padding * 2
                box_h = txt.get_height() + padding * 2
                # Semi-transparent background
                try:
                    import pygame
                    bg = pygame.Surface((box_w, box_h), pygame.SRCALPHA)
                    bg.fill((0, 0, 0, 180))
                    display_frame.blit(bg, (box_x - padding, box_y - padding))
                except Exception:
                    pass
                display_frame.blit(txt, (box_x, box_y))
                y_offset += txt.get_height() + int(2 * overrender)
        except Exception:
            pass
