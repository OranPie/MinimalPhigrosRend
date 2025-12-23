from __future__ import annotations

import math
from typing import Tuple, Optional, Any, Dict

from ..types import RuntimeLine, RuntimeNote
from ..math.util import clamp
from .. import state  # Kept for backward compatibility in note_world_pos

def eval_line_state(
    line: RuntimeLine,
    t: float,
    force_line_alpha01: Optional[float] = None,
    force_line_alpha01_by_lid: Optional[Dict[int, float]] = None,
) -> Tuple[float, float, float, float, float, float]:
    """Evaluate line state at time t.

    Args:
        line: RuntimeLine to evaluate
        t: Time
        force_line_alpha01: Force all lines to this alpha (0-1)
        force_line_alpha01_by_lid: Force specific line IDs to specific alpha values

    Returns:
        Tuple of (x, y, rotation, alpha01, scroll, alpha_raw)
    """
    x = line.pos_x.eval(t) if hasattr(line.pos_x, "eval") else float(line.pos_x(t))
    y = line.pos_y.eval(t) if hasattr(line.pos_y, "eval") else float(line.pos_y(t))
    rot = line.rot.eval(t) if hasattr(line.rot, "eval") else float(line.rot(t))
    a_raw = line.alpha.eval(t) if hasattr(line.alpha, "eval") else float(line.alpha(t))
    s = line.scroll_px.integral(t)
    a01 = clamp(abs(a_raw), 0.0, 1.0)

    # Apply force_line_alpha01_by_lid override
    if force_line_alpha01_by_lid is not None:
        try:
            if int(line.lid) in force_line_alpha01_by_lid:
                forced = clamp(float(force_line_alpha01_by_lid[int(line.lid)]), 0.0, 1.0)
                a01 = forced
                a_raw = forced
        except:
            pass

    # Apply force_line_alpha01 override
    if force_line_alpha01 is not None:
        try:
            forced = clamp(float(force_line_alpha01), 0.0, 1.0)
            a01 = forced
            a_raw = forced
        except:
            pass

    return x, y, rot, a01, s, a_raw

def note_world_pos(
    line_x: float,
    line_y: float,
    rot: float,
    scroll_now: float,
    note: RuntimeNote,
    scroll_target: float,
    for_tail: bool = False,
    note_flow_speed_multiplier: float = 1.0,
    note_speed_mul_affects_travel: bool = False,
    respack: Optional[Any] = None,
) -> Tuple[float, float]:
    """Calculate note world position.

    Args:
        line_x: Line X position
        line_y: Line Y position
        rot: Line rotation (radians)
        scroll_now: Current scroll value
        note: RuntimeNote
        scroll_target: Target scroll value
        for_tail: Whether this is for hold tail
        note_flow_speed_multiplier: Flow speed multiplier
        note_speed_mul_affects_travel: Whether speed_mul affects travel
        respack: Resource pack (for hold_keep_head check)

    Returns:
        Tuple of (world_x, world_y)
    """
    # tangent & normal
    tx, ty = math.cos(rot), math.sin(rot)
    nx, ny = -math.sin(rot), math.cos(rot)

    # direction
    sgn = 1.0 if note.above else -1.0

    # local offsets
    x_local = note.x_local_px
    # y-local: approach along normal based on scroll delta
    dy = (scroll_target - scroll_now)

    # Apply flow speed multiplier
    try:
        dy *= float(note_flow_speed_multiplier or 1.0)
    except:
        pass

    # holdKeepHead: head touches line and doesn't "pass through" line (dy not allowed to be negative)
    if respack and hasattr(respack, 'hold_keep_head') and respack.hold_keep_head and (note.kind == 3) and (not for_tail):
        if dy < 0.0:
            dy = 0.0

    mult = 1.0
    if for_tail and note.kind == 3:
        mult = max(0.0, note.speed_mul)
    elif (not for_tail) and (note.kind != 3) and note_speed_mul_affects_travel:
        mult = max(0.0, note.speed_mul)
    # For RPE, speed_mul often applied to travel; for Official we apply to hold tail only (above)
    y_local = sgn * dy * mult + note.y_offset_px

    # world
    x = line_x + tx * x_local + nx * y_local
    y = line_y + ty * x_local + ny * y_local
    return x, y


