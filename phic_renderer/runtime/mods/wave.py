from __future__ import annotations

import math
from typing import Any, Dict, List

from ...types import RuntimeLine, RuntimeNote
from .base import match_note_filter, parse_float


def apply_wave(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    """Wave mode: create wave patterns in note positioning.

    Applies sinusoidal wave patterns to note positions, sizes, or other properties
    based on time or note index.

    Config:
        wave:
            enable: true
            mode: "time"  # "time" (based on hit time) or "index" (based on note order)
            axis: "x"  # "x" (horizontal), "y" (vertical), "size", "alpha", "speed"
            amplitude: 100  # Wave amplitude (units depend on axis)
            frequency: 1.0  # Wave frequency (cycles per second for time, cycles per N notes for index)
            phase: 0.0  # Phase offset (0-1, as fraction of cycle)
            offset: 0.0  # DC offset added to wave
            filter:  # Optional: only apply to matching notes
                kinds: [1, 2]
    """
    cfg = None
    for k in ("wave", "sine", "oscillate"):
        if k in mods_cfg:
            cfg = mods_cfg.get(k)
            break

    if not (isinstance(cfg, dict) and bool(cfg.get("enable", True))):
        return notes

    # Parse mode
    mode = str(cfg.get("mode", "time")).strip().lower()
    axis = str(cfg.get("axis", "x")).strip().lower()

    # Parse wave parameters
    try:
        amplitude = float(cfg.get("amplitude", 100))
    except Exception:
        amplitude = 100.0

    try:
        frequency = float(cfg.get("frequency", cfg.get("freq", 1.0)))
    except Exception:
        frequency = 1.0

    try:
        phase = float(cfg.get("phase", 0.0))
    except Exception:
        phase = 0.0

    try:
        dc_offset = float(cfg.get("offset", cfg.get("dc_offset", 0.0)))
    except Exception:
        dc_offset = 0.0

    filter_cfg = cfg.get("filter", cfg.get("match", None))

    # Collect matching notes
    matching_notes = []
    for n in notes:
        if n.fake:
            continue

        should_wave = True
        if isinstance(filter_cfg, dict):
            should_wave = match_note_filter(n, filter_cfg)

        if should_wave:
            matching_notes.append(n)

    if not matching_notes:
        return notes

    # Apply wave pattern
    for idx, n in enumerate(matching_notes):
        # Calculate wave input
        if mode == "time":
            # Based on hit time
            wave_input = float(n.t_hit) * frequency
        elif mode == "index":
            # Based on note index
            wave_input = float(idx) * frequency / 10.0  # Scale for reasonable frequency
        else:
            wave_input = float(n.t_hit) * frequency

        # Calculate wave value: amplitude * sin(2π * (input + phase)) + offset
        wave_value = amplitude * math.sin(2.0 * math.pi * (wave_input + phase)) + dc_offset

        # Apply to appropriate axis
        if axis == "x":
            n.x_local_px = float(n.x_local_px) + wave_value
        elif axis == "y":
            n.y_offset_px = float(n.y_offset_px) + wave_value
        elif axis == "size":
            # Multiplicative for size
            size_mul = 1.0 + (wave_value / 100.0)  # Normalize amplitude
            n.size_px = float(n.size_px) * max(0.1, size_mul)
        elif axis == "alpha":
            # Additive for alpha, clamped to 0-1
            n.alpha01 = max(0.0, min(1.0, float(n.alpha01) + wave_value))
        elif axis == "speed":
            # Multiplicative for speed
            speed_mul = 1.0 + (wave_value / 100.0)  # Normalize amplitude
            n.speed_mul = float(n.speed_mul) * max(0.1, speed_mul)

    return notes
