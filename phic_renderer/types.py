from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, Optional, Tuple

@dataclass
class RuntimeNote:
    nid: int
    line_id: int
    kind: int             # 1 tap, 2 drag, 3 hold, 4 flick
    above: bool
    fake: bool
    t_hit: float
    t_end: float
    x_local_px: float     # along tangent
    y_offset_px: float
    speed_mul: float      # hold tail multiplier (official), general multiplier (rpe)
    size_px: float        # width scale
    alpha01: float

    tint_rgb: Tuple[int, int, int] = (255, 255, 255)
    tint_hitfx_rgb: Optional[Tuple[int, int, int]] = None

    # cached scroll at key times (pixel scroll)
    scroll_hit: float = 0.0
    scroll_end: float = 0.0

    # RPE custom hitsound + precomputed visibility
    hitsound_path: Optional[str] = None  # RPE: custom hitsound path
    t_enter: float = -1e9                # first time note enters screen (precomputed)
    mh: bool = False                     # multi-hit: for simultaneous notes (hold_mh)


@dataclass
class RuntimeLine:
    lid: int
    pos_x: Any            # track: eval(t)->px
    pos_y: Any            # track: eval(t)->px
    rot: Any              # track: eval(t)->rad
    alpha: Any            # track: eval(t)->0..1
    scroll_px: Any        # IntegralTrack: integral(t)->px
    color_rgb: Tuple[int, int, int]
    color: Optional[Any] = None   # track: eval(t)->(r,g,b)
    scale_x: Optional[Any] = None # track: eval(t)->float
    scale_y: Optional[Any] = None # track: eval(t)->float
    text: Optional[Any] = None    # track: eval(t)->str
    texture_path: Optional[str] = None
    anchor: Tuple[float, float] = (0.5, 0.5)
    is_gif: bool = False
    gif_progress: Optional[Any] = None  # track: eval(t)->0..1
    father: int = -1
    rotate_with_father: bool = True
    name: str = ""
    event_counts: Dict[str, int] = field(default_factory=dict)


@dataclass
class NoteState:
    note: RuntimeNote
    judged: bool = False
    hit: bool = False
    holding: bool = False
    released_early: bool = False
    miss: bool = False
    next_hold_fx_ms: int = 0
    hold_grade: Optional[str] = None
    hold_finalized: bool = False
    hold_failed: bool = False
