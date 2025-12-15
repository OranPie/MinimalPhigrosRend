from __future__ import annotations

import json
from typing import Any, Dict, List, Tuple

from .types import RuntimeLine, RuntimeNote
from .official import load_official
from .rpe import load_rpe

def detect_format(data: Dict[str, Any]) -> str:
    if "META" in data and "BPMList" in data and "judgeLineList" in data:
        return "rpe"
    if "formatVersion" in data and "judgeLineList" in data:
        return "official"
    # heuristic
    if "judgeLineList" in data and any("eventLayers" in jl for jl in data["judgeLineList"]):
        return "rpe"
    return "official"

def load_chart(path: str, W: int, H: int) -> Tuple[str, float, List[RuntimeLine], List[RuntimeNote]]:
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    fmt = detect_format(data)
    if fmt == "official":
        offset, lines, notes = load_official(data, W, H)
    else:
        offset, lines, notes = load_rpe(data, W, H)
    return fmt, offset, lines, notes
