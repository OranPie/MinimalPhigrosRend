from __future__ import annotations

from typing import Any, Dict, List

from ... import state
from ...types import RuntimeNote
from .base import parse_alpha01, apply_note_side


def apply_full_blue_mode(mods_cfg: Dict[str, Any], notes: List[RuntimeNote]) -> List[RuntimeNote]:
    full_blue_cfg = None
    for k in ("full_blue", "full_blue_mode", "fullbluemode", "FullBlueMode"):
        if k in mods_cfg:
            full_blue_cfg = mods_cfg.get(k)
            break

    if not (isinstance(full_blue_cfg, dict) and bool(full_blue_cfg.get("enable", True))):
        return notes

    la_force = full_blue_cfg.get("force_line_alpha", 255)
    a01 = parse_alpha01(la_force)
    if a01 is not None:
        state.force_line_alpha01 = a01

    if bool(full_blue_cfg.get("note_speed_mul_affects_travel", True)):
        state.note_speed_mul_affects_travel = True

    if bool(full_blue_cfg.get("convert_non_hold_to_tap", True)):
        for n in notes:
            if n.kind != 3:
                n.kind = 1

    note_ov = full_blue_cfg.get("note_overrides", {})
    if isinstance(note_ov, dict) and note_ov:
        apply_to_hold = bool(note_ov.get("apply_to_hold", True))
        force_speed = note_ov.get("speed_mul", None)
        force_alpha = note_ov.get("alpha", None)
        force_side = note_ov.get("side", None)
        force_size = note_ov.get("size", None)

        alpha01_force = parse_alpha01(force_alpha)
        try:
            speed_force = None if force_speed is None else float(force_speed)
        except:
            speed_force = None
        try:
            size_force = None if force_size is None else float(force_size)
        except:
            size_force = None

        for n in notes:
            if (not apply_to_hold) and n.kind == 3:
                continue
            if speed_force is not None:
                n.speed_mul = speed_force
            if alpha01_force is not None:
                n.alpha01 = alpha01_force
            if size_force is not None:
                n.size_px = size_force
            apply_note_side(n, force_side)

    return notes
