from __future__ import annotations

import random
from typing import Any, Dict, List

from ...types import RuntimeLine, RuntimeNote
from .base import match_note_filter


def apply_thin_out(mods_cfg: Dict[str, Any], notes: List[RuntimeNote], lines: List[RuntimeLine]) -> List[RuntimeNote]:
    """Thin out mode: remove notes by pattern or randomly to reduce density.

    Example: remove every 2nd note, or remove 30% of notes randomly.

    Config:
        thin_out:
            enable: true
            mode: "every"  # "every", "random", "keep" (default: "every")
            every: 2  # Remove every Nth note (keep 1, remove 1, keep 1, ...)
            offset: 0  # Starting offset for "every" mode
            probability: 0.3  # Probability to remove each note (0-1) for "random" mode
            keep_count: 100  # Keep only first N notes for "keep" mode
            seed: 12345  # Random seed for "random" mode (default: None)
            filter:  # Optional: only thin out matching notes
                kinds: [1, 2]
    """
    cfg = None
    for k in ("thin_out", "thin", "remove", "reduce"):
        if k in mods_cfg:
            cfg = mods_cfg.get(k)
            break

    if not (isinstance(cfg, dict) and bool(cfg.get("enable", True))):
        return notes

    # Parse mode
    mode = str(cfg.get("mode", "every")).strip().lower()

    # Parse parameters
    try:
        every = int(cfg.get("every", 2))
    except Exception:
        every = 2

    try:
        offset = int(cfg.get("offset", 0))
    except Exception:
        offset = 0

    try:
        probability = float(cfg.get("probability", cfg.get("remove_chance", 0.3)))
    except Exception:
        probability = 0.3

    try:
        keep_count = int(cfg.get("keep_count", cfg.get("keep", 100)))
    except Exception:
        keep_count = 100

    seed = cfg.get("seed", None)
    if seed is not None and mode == "random":
        try:
            random.seed(int(seed))
        except Exception:
            pass

    filter_cfg = cfg.get("filter", cfg.get("match", None))
    out_notes: List[RuntimeNote] = []
    match_idx = 0

    for n in notes:
        # Check if note matches filter
        should_process = True
        if isinstance(filter_cfg, dict):
            should_process = match_note_filter(n, filter_cfg)

        if not should_process:
            # Keep notes that don't match filter
            out_notes.append(n)
            continue

        # Apply thinning based on mode
        keep = True
        if mode == "every":
            # Keep note if (match_idx - offset) % every == 0
            if (match_idx - offset) % every != 0:
                keep = False
        elif mode == "random":
            # Use note ID as additional seed for deterministic randomness
            if seed is not None:
                random.seed(int(seed) + int(n.nid) * 31337)
            if random.random() < probability:
                keep = False
        elif mode == "keep":
            # Keep only first N matching notes
            if match_idx >= keep_count:
                keep = False

        if keep:
            out_notes.append(n)

        match_idx += 1

    return out_notes
