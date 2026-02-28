from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, List, Optional, Tuple

from .util import clamp, lerp

@dataclass
class EasedSeg:
    t0: float
    t1: float
    v0: float
    v1: float
    easing: Callable[[float], float]
    # optional clip window in [0,1]
    L: float = 0.0
    R: float = 1.0

class PiecewiseEased:
    def __init__(self, segs: List[EasedSeg], default: float = 0.0):
        self.segs = segs
        self.default = default
        self.i = 0

    def _seek(self, t: float):
        if not self.segs:
            self.i = 0
            return
        i = self.i
        while i + 1 < len(self.segs) and t >= self.segs[i].t1:
            i += 1
        while i > 0 and t < self.segs[i].t0:
            i -= 1
        self.i = i

    def eval(self, t: float) -> float:
        if not self.segs:
            return self.default
        self._seek(t)
        s = self.segs[self.i]
        if t <= s.t0:
            return s.v0
        if t >= s.t1:
            return s.v1
        p_raw = (t - s.t0) / (s.t1 - s.t0)
        # clip L/R
        if p_raw <= s.L: p = 0.0
        elif p_raw >= s.R: p = 1.0
        else: p = (p_raw - s.L) / max(1e-9, (s.R - s.L))
        p = clamp(p, 0.0, 1.0)
        e = s.easing(p)
        return lerp(s.v0, s.v1, e)

class SumTrack:
    def __init__(self, tracks: List[PiecewiseEased], default=0.0):
        self.tracks = tracks
        self.default = default

    def eval(self, t: float) -> float:
        if not self.tracks:
            return self.default
        return sum(tr.eval(t) for tr in self.tracks)



@dataclass
class Seg1D:
    t0: float
    t1: float
    v0: float
    v1: float
    prefix: float  # integral from 0 to t0

class IntegralTrack:
    def __init__(self, segs: List[Seg1D]):
        self.segs = segs
        self.i = 0

    def _seek(self, t: float):
        if not self.segs:
            self.i = 0
            return
        i = self.i
        while i + 1 < len(self.segs) and t >= self.segs[i].t1:
            i += 1
        while i > 0 and t < self.segs[i].t0:
            i -= 1
        self.i = i

    def integral(self, t: float) -> float:
        if not self.segs:
            return 0.0
        self._seek(t)
        s = self.segs[self.i]
        if t <= s.t0:
            return s.prefix
        if t >= s.t1:
            dt = s.t1 - s.t0
            area = 0.5 * (s.v0 + s.v1) * dt
            return s.prefix + area
        dt = t - s.t0
        full = s.t1 - s.t0
        u = dt / max(1e-9, full)
        vt = lerp(s.v0, s.v1, u)
        area = 0.5 * (s.v0 + vt) * dt
        return s.prefix + area


@dataclass
class ColorSeg:
    t0: float
    t1: float
    c0: Tuple[int, int, int]
    c1: Tuple[int, int, int]
    easing: Callable[[float], float]
    L: float = 0.0
    R: float = 1.0


class PiecewiseColor:
    def __init__(self, segs: List[ColorSeg], default: Tuple[int, int, int] = (255, 255, 255)):
        self.segs = segs
        self.default = default
        self.i = 0

    def _seek(self, t: float):
        if not self.segs:
            self.i = 0
            return
        i = self.i
        while i + 1 < len(self.segs) and t >= self.segs[i].t1:
            i += 1
        while i > 0 and t < self.segs[i].t0:
            i -= 1
        self.i = i

    def eval(self, t: float) -> Tuple[int, int, int]:
        if not self.segs:
            return self.default
        self._seek(t)
        s = self.segs[self.i]
        if t <= s.t0:
            return s.c0
        if t >= s.t1:
            return s.c1
        p_raw = (t - s.t0) / (s.t1 - s.t0)
        if p_raw <= s.L:
            p = 0.0
        elif p_raw >= s.R:
            p = 1.0
        else:
            p = (p_raw - s.L) / max(1e-9, (s.R - s.L))
        p = clamp(p, 0.0, 1.0)
        e = s.easing(p)
        r = int(lerp(float(s.c0[0]), float(s.c1[0]), e))
        g = int(lerp(float(s.c0[1]), float(s.c1[1]), e))
        b = int(lerp(float(s.c0[2]), float(s.c1[2]), e))
        return (clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255))


@dataclass
class TextSeg:
    t0: float
    t1: float
    s0: str
    s1: str


class PiecewiseText:
    def __init__(self, segs: List[TextSeg], default: str = ""):
        self.segs = segs
        self.default = default
        self.i = 0

    def _seek(self, t: float):
        if not self.segs:
            self.i = 0
            return
        i = self.i
        while i + 1 < len(self.segs) and t >= self.segs[i].t1:
            i += 1
        while i > 0 and t < self.segs[i].t0:
            i -= 1
        self.i = i

    def eval(self, t: float) -> str:
        if not self.segs:
            return self.default
        self._seek(t)
        s = self.segs[self.i]
        if t <= s.t0:
            return s.s0
        if t >= s.t1:
            return s.s1
        if s.s0 == s.s1:
            return s.s0
        mid = (s.t0 + s.t1) * 0.5
        return s.s0 if t < mid else s.s1


