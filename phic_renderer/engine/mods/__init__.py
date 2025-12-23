from __future__ import annotations

from typing import Any, Dict, List

from ...types import RuntimeLine, RuntimeNote
from .visual import apply_visual_mods
from .full_blue import apply_full_blue_mode
from .hold_convert import apply_hold_to_tap_drag
from .rules import apply_note_rules, apply_line_rules


def apply_mods(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    if not isinstance(mods_cfg, dict) or not mods_cfg:
        return notes

    apply_visual_mods(mods_cfg)
    notes = apply_full_blue_mode(mods_cfg, notes)
    notes = apply_hold_to_tap_drag(mods_cfg, notes, lines)
    apply_note_rules(mods_cfg, notes)
    apply_line_rules(mods_cfg, lines)

    return notes
