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
    notes_out = notes

    # Visual mods (don't modify notes)
    if mods_cfg.get("visual"):
        apply_visual_mods(mods_cfg["visual"], notes_out, lines)

    # Full blue mode
    if mods_cfg.get("full_blue"):
        from .full_blue import apply_full_blue_mode
        notes_out = apply_full_blue_mode(notes_out)

    # Hold conversion
    if mods_cfg.get("hold_convert"):
        notes_out = apply_hold_to_tap_drag(notes_out, mods_cfg["hold_convert"])

    # Note/Line rules
    if mods_cfg.get("note_rules"):
        notes_out = apply_note_rules(notes_out, mods_cfg["note_rules"])
    if mods_cfg.get("line_rules"):
        apply_line_rules(lines, mods_cfg["line_rules"])

    # Compress zip
    if mods_cfg.get("compress_zip"):
        notes_out = apply_compress_zip(notes_out, lines, mods_cfg["compress_zip"])

    # Attach
    if mods_cfg.get("attach"):
        notes_out = apply_attach(notes_out, lines, mods_cfg["attach"])

    # Mirror
    if mods_cfg.get("mirror"):
        notes_out = apply_mirror(notes_out, mods_cfg["mirror"])

    # Randomize
    if mods_cfg.get("randomize"):
        notes_out = apply_randomize(notes_out, mods_cfg["randomize"])

    # Scale
    if mods_cfg.get("scale"):
        notes_out = apply_scale(notes_out, mods_cfg["scale"])

    # Fade
    if mods_cfg.get("fade"):
        notes_out = apply_fade(notes_out, mods_cfg["fade"])

    # Thin out
    if mods_cfg.get("thin_out"):
        notes_out = apply_thin_out(notes_out, mods_cfg["thin_out"])

    # Quantize
    if mods_cfg.get("quantize"):
        notes_out = apply_quantize(notes_out, mods_cfg["quantize"])

    # Transpose
    if mods_cfg.get("transpose"):
        notes_out = apply_transpose(notes_out, mods_cfg["transpose"])

    # Stretch
    if mods_cfg.get("stretch"):
        notes_out = apply_stretch(notes_out, mods_cfg["stretch"])

    # Reverse
    if mods_cfg.get("reverse"):
        notes_out = apply_reverse(notes_out, mods_cfg["reverse"])

    # Colorize
    if mods_cfg.get("colorize"):
        notes_out = apply_colorize(notes_out, mods_cfg["colorize"])

    # Wave
    if mods_cfg.get("wave"):
        notes_out = apply_wave(notes_out, mods_cfg["wave"])

    # Stutter
    if mods_cfg.get("stutter"):
        notes_out = apply_stutter(notes_out, mods_cfg["stutter"])

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
