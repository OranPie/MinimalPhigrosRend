from __future__ import annotations

from typing import List

import pygame

from ...runtime.effects import ParticleBurst
from ...math.util import apply_expand_xy


def draw_particles(screen: pygame.Surface, particles: List[ParticleBurst], now_ms: int, W: int, H: int, expand: float):
    if hasattr(pygame, "BLEND_RGBA_ADD"):
        blend_flag = pygame.BLEND_RGBA_ADD
    elif hasattr(pygame, "BLEND_ADD"):
        blend_flag = pygame.BLEND_ADD
    else:
        blend_flag = 0

    for p in particles:
        parts = p.get_particles(now_ms)
        for q in parts:
            xq, yq = apply_expand_xy(q["x"], q["y"], W, H, expand)
            sz = max(1, int(q["size"] / float(expand)))
            sq = pygame.Surface((sz, sz), pygame.SRCALPHA)
            sq.fill(q["color"])
            screen.blit(sq, (int(xq - sz / 2), int(yq - sz / 2)), special_flags=blend_flag)
