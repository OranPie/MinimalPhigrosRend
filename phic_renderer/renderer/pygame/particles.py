from __future__ import annotations

from typing import List, Dict, Tuple
from collections import defaultdict

import pygame

from ...runtime.effects import ParticleBurst
from ...math.util import apply_expand_xy


# Pre-generated particle surfaces cache
_particle_cache: Dict[Tuple[int, Tuple[int, int, int, int]], pygame.Surface] = {}


def _get_particle_surface(size: int, color: Tuple[int, int, int, int]) -> pygame.Surface:
    """
    Get or create a cached particle surface.

    Args:
        size: Particle size in pixels
        color: RGBA color tuple

    Returns:
        Pre-rendered particle surface
    """
    key = (size, color)
    if key not in _particle_cache:
        surf = pygame.Surface((size, size), pygame.SRCALPHA)
        surf.fill(color)
        _particle_cache[key] = surf

        # Limit cache size to prevent memory growth
        if len(_particle_cache) > 200:
            # Clear cache if it grows too large
            _particle_cache.clear()
            surf = pygame.Surface((size, size), pygame.SRCALPHA)
            surf.fill(color)
            _particle_cache[key] = surf

    return _particle_cache[key]


def draw_particles(screen: pygame.Surface, particles: List[ParticleBurst], now_ms: int, W: int, H: int, expand: float):
    """
    Draw particles with batching optimization.

    Groups particles by (size, color) and batches blit operations
    for improved performance.
    """
    if hasattr(pygame, "BLEND_RGBA_ADD"):
        blend_flag = pygame.BLEND_RGBA_ADD
    elif hasattr(pygame, "BLEND_ADD"):
        blend_flag = pygame.BLEND_ADD
    else:
        blend_flag = 0

    # Batch particles by (size, color) for efficient rendering
    batches: Dict[Tuple[int, Tuple[int, int, int, int]], List[Tuple[float, float]]] = defaultdict(list)

    # Collect all particles into batches
    for p in particles:
        parts = p.get_particles(now_ms)
        for q in parts:
            xq, yq = apply_expand_xy(q["x"], q["y"], W, H, expand)
            sz = max(1, int(q["size"] / float(expand)))
            color = q["color"]

            # Ensure color is a tuple (RGBA)
            if not isinstance(color, tuple):
                color = (255, 255, 255, 255)
            elif len(color) == 3:
                color = (*color, 255)

            batch_key = (sz, color)
            batches[batch_key].append((xq, yq))

    # Render each batch
    for (sz, color), positions in batches.items():
        surf = _get_particle_surface(sz, color)

        # Blit all particles of this size/color in one pass
        for xq, yq in positions:
            screen.blit(surf, (int(xq - sz / 2), int(yq - sz / 2)), special_flags=blend_flag)
