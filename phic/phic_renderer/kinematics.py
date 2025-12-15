from __future__ import annotations

import math
from typing import Tuple

from .types import RuntimeLine, RuntimeNote
from . import state

def eval_line_state(line: RuntimeLine, t: float) -> Tuple[float, float, float, float, float, float]:
    x = line.pos_x.eval(t) if hasattr(line.pos_x, "eval") else float(line.pos_x(t))
    y = line.pos_y.eval(t) if hasattr(line.pos_y, "eval") else float(line.pos_y(t))
    rot = line.rot.eval(t) if hasattr(line.rot, "eval") else float(line.rot(t))
    a_raw = line.alpha.eval(t) if hasattr(line.alpha, "eval") else float(line.alpha(t))
    s = line.scroll_px.integral(t)
    a01 = clamp(abs(a_raw), 0.0, 1.0)
    return x, y, rot, a01, s, a_raw

def note_world_pos(line_x, line_y, rot, scroll_now, note: RuntimeNote, scroll_target, for_tail=False) -> Tuple[float, float]:
    # tangent & normal
    tx, ty = math.cos(rot), math.sin(rot)
    nx, ny = -math.sin(rot), math.cos(rot)

    # direction
    sgn = 1.0 if note.above else -1.0

    # local offsets
    x_local = note.x_local_px
    # y-local: approach along normal based on scroll delta
    dy = (scroll_target - scroll_now)

    # holdKeepHead: head touches line and doesn't "pass through" line (dy not allowed to be negative)
    if respack and respack.hold_keep_head and (note.kind == 3) and (not for_tail):
        if dy < 0.0:
            dy = 0.0

    mult = 1.0
    if for_tail and note.kind == 3:
        mult = max(0.0, note.speed_mul)
    # For RPE, speed_mul often applied to travel; for Official we apply to hold tail only (above)
    y_local = sgn * dy * mult + note.y_offset_px

    # world
    x = line_x + tx * x_local + nx * y_local
    y = line_y + ty * x_local + ny * y_local
    return x, y


