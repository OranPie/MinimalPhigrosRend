from __future__ import annotations

import random
from typing import Any, Dict, List

from ...types import RuntimeLine, RuntimeNote
from .base import match_note_filter, parse_float


def apply_randomize(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    """Randomize mode: add random variation to note properties.

    Example: randomize x position to add chaos, or randomize timing slightly.

    Config:
        randomize:
            enable: true
            seed: 12345  # Random seed for deterministic results (default: None)
            x_range: [-50, 50]  # Random x offset range in px
            y_range: [-20, 20]  # Random y offset range in px
            time_range: [-0.05, 0.05]  # Random time offset range in seconds
            speed_range: [0.8, 1.2]  # Random speed multiplier range
            size_range: [0.9, 1.1]  # Random size multiplier range
            alpha_range: [0.8, 1.0]  # Random alpha range
            flip_side_chance: 0.1  # Probability to flip above/below (0-1)
            filter:  # Optional: only randomize matching notes
                kinds: [1, 2]  # Only randomize tap and drag
    """
    cfg = None
    for k in ("randomize", "random", "chaos", "jitter"):
        if k in mods_cfg:
            cfg = mods_cfg.get(k)
            break

    if not (isinstance(cfg, dict) and bool(cfg.get("enable", True))):
        return notes

    # Parse seed
    seed = cfg.get("seed", None)
    if seed is not None:
        try:
            random.seed(int(seed))
        except Exception:
            pass

    # Parse ranges
    def parse_range(key: str, default_range: List[float]) -> tuple[float, float]:
        val = cfg.get(key, None)
        if val is None:
            return (default_range[0], default_range[1])
        if isinstance(val, (list, tuple)) and len(val) >= 2:
            try:
                return (float(val[0]), float(val[1]))
            except Exception:
                return (default_range[0], default_range[1])
        return (default_range[0], default_range[1])

    x_min, x_max = parse_range("x_range", [0.0, 0.0])
    y_min, y_max = parse_range("y_range", [0.0, 0.0])
    time_min, time_max = parse_range("time_range", [0.0, 0.0])
    speed_min, speed_max = parse_range("speed_range", [1.0, 1.0])
    size_min, size_max = parse_range("size_range", [1.0, 1.0])
    alpha_min, alpha_max = parse_range("alpha_range", [1.0, 1.0])

    try:
        flip_chance = float(cfg.get("flip_side_chance", cfg.get("flip_chance", 0)))
    except Exception:
        flip_chance = 0.0

    filter_cfg = cfg.get("filter", cfg.get("match", None))

    for n in notes:
        if n.fake:
            continue

        # Check if note matches filter
        should_randomize = True
        if isinstance(filter_cfg, dict):
            should_randomize = match_note_filter(n, filter_cfg)

        if not should_randomize:
            continue

        # Use note ID as additional seed component for deterministic randomness per note
        if seed is not None:
            random.seed(int(seed) + int(n.nid) * 31337)

        # Apply random offsets
        if x_min != 0.0 or x_max != 0.0:
            n.x_local_px = float(n.x_local_px) + random.uniform(float(x_min), float(x_max))

        if y_min != 0.0 or y_max != 0.0:
            n.y_offset_px = float(n.y_offset_px) + random.uniform(float(y_min), float(y_max))

        if time_min != 0.0 or time_max != 0.0:
            time_offset = random.uniform(float(time_min), float(time_max))
            n.t_hit = float(n.t_hit) + time_offset
            n.t_end = float(n.t_end) + time_offset

        if speed_min != 1.0 or speed_max != 1.0:
            n.speed_mul = float(n.speed_mul) * random.uniform(float(speed_min), float(speed_max))

        if size_min != 1.0 or size_max != 1.0:
            n.size_px = float(n.size_px) * random.uniform(float(size_min), float(size_max))

        if alpha_min != 1.0 or alpha_max != 1.0:
            n.alpha01 = max(0.0, min(1.0, float(n.alpha01) * random.uniform(float(alpha_min), float(alpha_max))))

        if flip_chance > 0:
            if random.random() < float(flip_chance):
                n.above = not bool(n.above)

    # Re-sort by hit time since timing may have changed
    return sorted(notes, key=lambda x: x.t_hit)
