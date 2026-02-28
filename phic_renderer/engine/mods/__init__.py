from __future__ import annotations

from typing import Any, Dict, List

from ...types import RuntimeLine, RuntimeNote
from .visual import apply_visual_mods
from .full_blue import apply_full_blue_mode
from .hold_convert import apply_hold_to_tap_drag
from .rules import apply_note_rules, apply_line_rules
from .compress_zip import apply_compress_zip
from .attach import apply_attach
from .mirror import apply_mirror
from .randomize import apply_randomize
from .scale import apply_scale
from .fade import apply_fade
from .thin_out import apply_thin_out
from .quantize import apply_quantize
from .transpose import apply_transpose
from .stretch import apply_stretch
from .reverse import apply_reverse
from .colorize import apply_colorize
from .wave import apply_wave
from .stutter import apply_stutter


def apply_mods(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    """Apply all enabled mods in sequence.

    Mod execution order:
    1. visual - Visual-only effects (no note/line modification)
    2. full_blue - Convert all notes to blue
    3. hold_convert - Convert holds to tap/drag
    4. note_rules/line_rules - Apply custom rules
    5. compress_zip - Compress note timing
    6. attach - Attach notes to moving lines
    7. mirror - Mirror chart horizontally
    8. randomize - Randomize note positions
    9. scale - Scale note positions
    10. fade - Fade notes in/out
    11. thin_out - Remove percentage of notes
    12. quantize - Quantize note timing
    13. transpose - Transpose note positions
    14. stretch - Stretch note timing
    15. reverse - Reverse note order
    16. colorize - Colorize notes
    17. wave - Apply wave effect to notes
    18. stutter - Add stutter effect
    """
    if not isinstance(mods_cfg, dict) or not mods_cfg:
        return notes

    notes_out = notes

    # Visual-only mods (no note/line modification)
    apply_visual_mods(mods_cfg)

    # Note transformation mods (order matters)
    notes_out = apply_full_blue_mode(mods_cfg, notes_out)
    notes_out = apply_hold_to_tap_drag(mods_cfg, notes_out, lines)

    # Timing transformations
    notes_out = apply_transpose(mods_cfg, notes_out, lines)
    notes_out = apply_stretch(mods_cfg, notes_out, lines)
    notes_out = apply_reverse(mods_cfg, notes_out, lines)

    # Position/property transformations
    notes_out = apply_quantize(mods_cfg, notes_out, lines)
    notes_out = apply_mirror(mods_cfg, notes_out, lines)
    notes_out = apply_scale(mods_cfg, notes_out, lines)
    notes_out = apply_wave(mods_cfg, notes_out, lines)
    notes_out = apply_randomize(mods_cfg, notes_out, lines)
    notes_out = apply_fade(mods_cfg, notes_out, lines)

    # Note generation/removal mods
    notes_out = apply_thin_out(mods_cfg, notes_out, lines)
    notes_out = apply_stutter(mods_cfg, notes_out, lines)
    notes_out = apply_compress_zip(mods_cfg, notes_out, lines)
    notes_out = apply_attach(mods_cfg, notes_out, lines)

    # Visual effects
    notes_out = apply_colorize(mods_cfg, notes_out, lines)

    # Rule-based mods
    apply_note_rules(mods_cfg, notes_out)
    apply_line_rules(mods_cfg, lines)

    return notes_out


__all__ = [
    "apply_mods",
    "apply_visual_mods",
    "apply_full_blue_mode",
    "apply_hold_to_tap_drag",
    "apply_note_rules",
    "apply_line_rules",
    "apply_compress_zip",
    "apply_attach",
    "apply_mirror",
    "apply_randomize",
    "apply_scale",
    "apply_fade",
    "apply_thin_out",
    "apply_quantize",
    "apply_transpose",
    "apply_stretch",
    "apply_reverse",
    "apply_colorize",
    "apply_wave",
    "apply_stutter",
]
