from __future__ import annotations

import math
import random as _rnd
from dataclasses import dataclass
from typing import List, Tuple

import pygame

@dataclass
class HitFX:
    x: float
    y: float
    t0: float
    rgba: Tuple[int, int, int, int]
    rot: float


class ParticleBurst:
    def __init__(self, x: float, y: float, start_ms: int, duration_ms: int,
                 rgba: Tuple[int, int, int, int], count: int = 4):
        import random as _rnd
        self.x, self.y = float(x), float(y)
        self.start = int(start_ms)
        self.duration = max(1, int(duration_ms))
        self.rgba = rgba
        self.pa = [(_rnd.uniform(185, 265), _rnd.uniform(0, 2 * math.pi))
                   for _ in range(max(1, count))]

    def alive(self, now_ms: int) -> bool:
        return now_ms < self.start + self.duration

    def get_particles(self, now_ms: int) -> List[Dict[str, Any]]:
        tick = (now_ms - self.start) / self.duration
        tick = 0.0 if tick < 0 else 1.0 if tick > 1 else tick
        alpha = int(255 * (1 - tick))
        size = 30 * (((0.2078 * tick - 1.6524) * tick + 1.6399) * tick + 0.4988)
        size = max(2, int(size))
        r, g, b, _ = self.rgba

        particles = []
        for spd, ang in self.pa:
            dist = spd * (9 * tick / (8 * tick + 1)) / 2
            px = self.x + dist * math.cos(ang)
            py = self.y + dist * math.sin(ang)
            particles.append({
                'x': int(px),
                'y': int(py),
                'size': size,
                'color': (r, g, b, alpha)
            })
        return particles

    def draw(self, surface: "pygame.Surface", now_ms: int) -> None:
        particles = self.get_particles(now_ms)
        if hasattr(pygame, "BLEND_RGBA_ADD"):
            blend_flag = pygame.BLEND_RGBA_ADD
        elif hasattr(pygame, "BLEND_ADD"):
            blend_flag = pygame.BLEND_ADD
        else:
            blend_flag = 0
        for p in particles:
            sq = pygame.Surface((p['size'], p['size']), pygame.SRCALPHA)
            sq.fill(p['color'])
            surface.blit(sq, (p['x'] - p['size']//2, p['y'] - p['size']//2), special_flags=blend_flag)
