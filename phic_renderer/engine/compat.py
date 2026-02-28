"""Backward-compatible wrappers for engine modules.

These wrappers allow old code to use the new engine modules with the legacy
global state pattern. New code should use the engine modules directly with
explicit parameters.
"""

from __future__ import annotations
from typing import List, Tuple, Optional

from .. import state
from ..types import RuntimeLine, RuntimeNote


def eval_line_state_compat(line: RuntimeLine, t: float) -> Tuple[float, float, float, float, float, float]:
    """Backward-compatible wrapper for eval_line_state using global state.

    DEPRECATED: Use engine.kinematics.eval_line_state with explicit parameters instead.
    """
    from .kinematics import eval_line_state
    return eval_line_state(
        line,
        t,
        force_line_alpha01=state.force_line_alpha01,
        force_line_alpha01_by_lid=state.force_line_alpha01_by_lid,
    )


def note_world_pos_compat(
    line_x: float,
    line_y: float,
    rot: float,
    scroll_now: float,
    note: RuntimeNote,
    scroll_target: float,
    for_tail: bool = False,
) -> Tuple[float, float]:
    """Backward-compatible wrapper for note_world_pos using global state.

    DEPRECATED: Use engine.kinematics.note_world_pos with explicit parameters instead.
    """
    from .kinematics import note_world_pos
    return note_world_pos(
        line_x,
        line_y,
        rot,
        scroll_now,
        note,
        scroll_target,
        for_tail=for_tail,
        note_flow_speed_multiplier=state.note_flow_speed_multiplier,
        note_speed_mul_affects_travel=state.note_speed_mul_affects_travel,
        respack=state.respack,
    )


__all__ = ["eval_line_state_compat", "note_world_pos_compat"]
