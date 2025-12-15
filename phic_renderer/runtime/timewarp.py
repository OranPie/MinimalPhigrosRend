from __future__ import annotations
from typing import Any

class _TimeWarpEval:
    def __init__(self, base: Any, start_at: float, speed: float, offset: float, time_offset: float):
        self.base = base
        self.start_at = float(start_at)
        self.speed = float(speed)
        self.offset = float(offset)
        self.time_offset = float(time_offset)

    def eval(self, t: float) -> float:
        lt = (float(t) - self.start_at) * self.speed - self.offset + self.time_offset
        if hasattr(self.base, "eval"):
            return float(self.base.eval(lt))
        return float(self.base(lt))


class _TimeWarpIntegral:
    def __init__(self, base: Any, start_at: float, speed: float, offset: float, time_offset: float):
        self.base = base
        self.start_at = float(start_at)
        self.speed = float(speed)
        self.offset = float(offset)
        self.time_offset = float(time_offset)
