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
    2. full_blue - Convert all notes to blue (above)
    3. hold_convert - Convert holds to tap/drag or vice versa
    4. transpose - Shift note timing forward or backward
    5. stretch - Stretch or compress chart timing
    6. reverse - Reverse note order in time
    7. quantize - Snap notes to grid (timing/position)
    8. mirror - Flip notes horizontally/vertically
    9. scale - Scale note sizes/positions/speeds
    10. wave - Create wave patterns in note positioning
    11. randomize - Add random variation
    12. fade - Modify note alpha based on time
    13. thin_out - Remove notes by pattern
    14. stutter - Create stutter/echo effects
    15. compress_zip - Duplicate notes at same position
    16. attach - Add offset notes to each note
    17. colorize - Apply color tints to notes
    18. note_rules - Apply per-note modification rules
    19. line_rules - Apply per-line modification rules
    """
    if not isinstance(mods_cfg, dict) or not mods_cfg:
        return notes

    # Visual-only mods (no note/line modification)
    apply_visual_mods(mods_cfg)

    # Note transformation mods (order matters!)
    notes = apply_full_blue_mode(mods_cfg, notes)
    notes = apply_hold_to_tap_drag(mods_cfg, notes, lines)

    # Timing transformations (apply early)
    notes = apply_transpose(mods_cfg, notes, lines)
    notes = apply_stretch(mods_cfg, notes, lines)
    notes = apply_reverse(mods_cfg, notes, lines)

    # Position/property transformations
    notes = apply_quantize(mods_cfg, notes, lines)
    notes = apply_mirror(mods_cfg, notes, lines)
    notes = apply_scale(mods_cfg, notes, lines)
    notes = apply_wave(mods_cfg, notes, lines)
    notes = apply_randomize(mods_cfg, notes, lines)
    notes = apply_fade(mods_cfg, notes, lines)

    # Note generation/removal mods
    notes = apply_thin_out(mods_cfg, notes, lines)
    notes = apply_stutter(mods_cfg, notes, lines)
    notes = apply_compress_zip(mods_cfg, notes, lines)
    notes = apply_attach(mods_cfg, notes, lines)

    # Visual effects (apply late)
    notes = apply_colorize(mods_cfg, notes, lines)

    # Rule-based mods (apply last for final adjustments)
    apply_note_rules(mods_cfg, notes)
    apply_line_rules(mods_cfg, lines)

    return notes
