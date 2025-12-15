from __future__ import annotations

from typing import List

from ..runtime.effects import HitFX, ParticleBurst


def prune_hitfx(hitfx: List[HitFX], t: float, duration: float) -> List[HitFX]:
    return [fx for fx in hitfx if (t - fx.t0) <= duration]


def prune_particles(particles: List[ParticleBurst], now_ms: int) -> List[ParticleBurst]:
    return [p for p in particles if p.alive(now_ms)]
