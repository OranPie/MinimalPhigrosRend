from __future__ import annotations

from collections import deque
from typing import Any, Optional, Tuple

import pygame

from ....math.util import clamp


def apply_trail(
    *,
    surface_pool: Any,
    W: int,
    H: int,
    display_frame_cur: pygame.Surface,
    trail_alpha: float,
    trail_frames: int,
    trail_decay: float,
    trail_blur: int,
    trail_blur_ramp: bool,
    trail_dim: int,
    trail_blend: str,
    trail_hist: Optional[deque],
    trail_hist_cap: int,
    trail_dim_cache: Optional[pygame.Surface],
    trail_dim_cache_key: Optional[Tuple[int, int, int]],
):
    """Apply trail effect and return (display_frame, trail_hist, trail_hist_cap, trail_dim_cache, trail_dim_cache_key)."""
    if float(trail_alpha) > 1e-6 and int(trail_frames) >= 1:
        if trail_hist is None:
            trail_hist = deque(maxlen=int(trail_frames))
            trail_hist_cap = int(trail_frames)
        if int(trail_frames) != int(trail_hist_cap):
            trail_hist = deque(list(trail_hist)[-int(trail_frames):], maxlen=int(trail_frames))
            trail_hist_cap = int(trail_frames)

        out = surface_pool.get(int(W), int(H), pygame.SRCALPHA)
        hist_list = list(trail_hist)
        for idx, frm in enumerate(hist_list):
            age = (len(hist_list) - 1) - idx
            w = float(trail_alpha) * (float(trail_decay) ** float(age))
            if w <= 1e-6:
                continue
            src = frm
            blur_k = int(trail_blur)
            if trail_blur_ramp and blur_k > 1:
                blur_k = int(max(2, blur_k * (1 + age)))
            if blur_k and blur_k > 1:
                bw = max(1, int(int(W) / blur_k))
                bh = max(1, int(int(H) / blur_k))
                src = pygame.transform.smoothscale(src, (bw, bh))
                src = pygame.transform.smoothscale(src, (int(W), int(H)))
            if int(trail_dim) > 0:
                dkey = (int(W), int(H), int(trail_dim))
                if (trail_dim_cache is None) or (trail_dim_cache_key != dkey):
                    trail_dim_cache = pygame.Surface((int(W), int(H)), pygame.SRCALPHA)
                    trail_dim_cache.fill((0, 0, 0, int(trail_dim)))
                    trail_dim_cache_key = dkey
                src = src.copy()
                src.blit(trail_dim_cache, (0, 0))
            else:
                src = src.copy()
            src.set_alpha(int(255 * clamp(w, 0.0, 1.0)))
            if str(trail_blend) == "add":
                out.blit(src, (0, 0), special_flags=pygame.BLEND_RGBA_ADD)
            else:
                out.blit(src, (0, 0))

        out.blit(display_frame_cur, (0, 0))
        display_frame = out
        trail_hist.append(display_frame_cur)
        return display_frame, trail_hist, trail_hist_cap, trail_dim_cache, trail_dim_cache_key

    display_frame = display_frame_cur
    if trail_hist is not None:
        try:
            trail_hist.clear()
        except Exception:
            pass
    trail_dim_cache = None
    trail_dim_cache_key = None
    return display_frame, trail_hist, trail_hist_cap, trail_dim_cache, trail_dim_cache_key
