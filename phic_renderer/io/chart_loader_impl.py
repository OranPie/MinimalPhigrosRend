from __future__ import annotations

import json
import os
from typing import Any, Dict, List, Tuple

from ..types import RuntimeLine, RuntimeNote
from ..formats.official_impl import load_official
from ..formats.rpe_impl import load_rpe
from ..formats.pec_impl import load_pec, load_pec_text


def detect_format(data: Dict[str, Any]) -> str:
    if "META" in data and "BPMList" in data and "judgeLineList" in data:
        return "rpe"
    if "formatVersion" in data and "judgeLineList" in data:
        return "official"
    if "judgeLineList" in data and any("eventLayers" in jl for jl in data["judgeLineList"]):
        return "rpe"
    return "official"


def load_chart(path: str, W: int, H: int) -> Tuple[str, float, List[RuntimeLine], List[RuntimeNote]]:
    if str(path).lower().endswith((".pec", ".pe")):
        offset, lines, notes = load_pec(path, W, H)
        return "pec", offset, lines, notes

    # Try JSON first; if it fails, fall back to PEC text parsing.
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except json.JSONDecodeError:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        offset, lines, notes = load_pec_text(text, W, H)
        return "pec", offset, lines, notes
    fmt = detect_format(data)
    if fmt == "official":
        offset, lines, notes = load_official(data, W, H)
    else:
        offset, lines, notes = load_rpe(data, W, H)
    return fmt, offset, lines, notes
