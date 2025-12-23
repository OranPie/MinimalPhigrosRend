from __future__ import annotations

from typing import Any, Callable, List, Tuple

import pygame


def apply_motion_blur(
    *,
    t: float,
    dt_frame: float,
    chart_speed: float,
    mb_samples: int,
    mb_shutter: float,
    W: int,
    H: int,
    render_frame_cb: Callable[[float], Tuple[pygame.Surface, List[Any]]],
    surface_pool: Any,
):
    """Apply motion blur by sampling multiple sub-frames and accumulating.

    - render_frame_cb(t_sample) must return (base_surface, line_text_draw_calls)
    - The returned surface is the final W/H sized surface.

    Returns: display_frame_cur (pygame.Surface)
    """
    if int(mb_samples) <= 1 or float(mb_shutter) <= 1e-6:
        b0, _ = render_frame_cb(float(t))
        f0 = pygame.transform.smoothscale(b0, (int(W), int(H)))
        try:
            surface_pool.release(b0)
        except Exception:
            pass
        return f0

    acc = pygame.Surface((int(W), int(H)), pygame.SRCALPHA)
    acc.fill((0, 0, 0, 0))
    dt_chart = float(dt_frame) * float(chart_speed)

    for i in range(int(mb_samples)):
        frac = 0.0 if int(mb_samples) <= 1 else (float(i) / float(int(mb_samples) - 1))
        t_s = float(t) - float(mb_shutter) * float(dt_chart) * (1.0 - float(frac))
        b_i, _ = render_frame_cb(float(t_s))
        f_i = pygame.transform.smoothscale(b_i, (int(W), int(H)))
        try:
            surface_pool.release(b_i)
        except Exception:
            pass
        try:
            f_i.set_alpha(int(255 / float(int(mb_samples))))
        except Exception:
            pass
        acc.blit(f_i, (0, 0), special_flags=pygame.BLEND_RGBA_ADD)

    return acc
