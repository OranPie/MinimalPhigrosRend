from __future__ import annotations

import math
import time

def clamp(x, a, b):
    return a if x < a else b if x > b else x

def lerp(a, b, t):
    return a + (b - a) * t

def now_sec():
    return time.perf_counter()

def hsv_to_rgb(h, s, v):
    # h: 0..1
    i = int(h * 6.0)
    f = h * 6.0 - i
    p = v * (1.0 - s)
    q = v * (1.0 - f * s)
    t = v * (1.0 - (1.0 - f) * s)
    i = i % 6
    if i == 0: r, g, b = v, t, p
    elif i == 1: r, g, b = q, v, p
    elif i == 2: r, g, b = p, v, t
    elif i == 3: r, g, b = p, q, v
    elif i == 4: r, g, b = t, p, v
    else: r, g, b = v, p, q
    return int(r*255), int(g*255), int(b*255)

def rotate_vec(x, y, ang):
    c = math.cos(ang); s = math.sin(ang)
    return (c*x - s*y, s*x + c*y)

def rect_corners(cx, cy, w, h, ang):
    # returns 4 points (x,y) for a rotated rect centered at (cx,cy)
    hx, hy = w * 0.5, h * 0.5
    pts = [(-hx, -hy), (hx, -hy), (hx, hy), (-hx, hy)]
    out = []
    c = math.cos(ang); s = math.sin(ang)
    for px, py in pts:
        rx = c*px - s*py
        ry = s*px + c*py
        out.append((cx + rx, cy + ry))
    return out

def apply_expand_xy(x: float, y: float, W: int, H: int, expand: float) -> tuple[float, float]:
    if expand is None or expand <= 1.000001:
        return float(x), float(y)
    cx = W * 0.5
    cy = H * 0.5
    s = 1.0 / float(expand)
    return (cx + (float(x) - cx) * s, cy + (float(y) - cy) * s)

def apply_expand_pts(pts, W: int, H: int, expand: float):
    if expand is None or expand <= 1.000001:
        return pts
    return [apply_expand_xy(px, py, W, H, expand) for (px, py) in pts]
