from __future__ import annotations

import math
from typing import Any

import pygame

from ...runtime.effects import HitFX
from ...math.util import apply_expand_xy, clamp
from .draw import draw_ring


def draw_hitfx(overlay: pygame.Surface, fx: HitFX, t: float, *, respack: Any, W: int, H: int, expand: float, hitfx_scale_mul: float):
    if not respack:
        age = t - fx.t0
        if age < 0 or age > 0.18:
            return
        r = int(10 + 140 * age)
        a = int(255 * (1.0 - age / 0.18))
        rr, gg, bb, _ = fx.rgba
        x0, y0 = apply_expand_xy(fx.x, fx.y, W, H, expand)
        draw_ring(overlay, x0, y0, max(1, int(r / expand)), (rr, gg, bb, a), thickness=3)
        return

    age = t - fx.t0
    dur = max(1e-6, respack.hitfx_duration)
    if age < 0 or age > dur:
        return

    fw, fh = respack.hitfx_frames_xy
    sheet = respack.hitfx_sheet
    sw, sh = sheet.get_width(), sheet.get_height()
    cell_w, cell_h = sw // fw, sh // fh

    p = clamp(age / dur, 0.0, 0.999999)
    idx = int(p * (fw * fh))
    ix = idx % fw
    iy = idx // fw

    frame = sheet.subsurface((ix * cell_w, iy * cell_h, cell_w, cell_h))

    sc = (respack.hitfx_scale * float(hitfx_scale_mul)) / float(expand)
    if sc != 1.0:
        frame = pygame.transform.smoothscale(frame, (int(cell_w * sc), int(cell_h * sc)))

    if respack.hitfx_rotate:
        frame = pygame.transform.rotozoom(frame, -fx.rot * 180.0 / math.pi, 1.0)

    r, g, b, a = fx.rgba
    if respack.hitfx_tinted or (r, g, b) != (255, 255, 255):
        tint_s = pygame.Surface(frame.get_size(), pygame.SRCALPHA)
        tint_s.fill((r, g, b, 255))
        frame = frame.copy()
        frame.blit(tint_s, (0, 0), special_flags=pygame.BLEND_RGBA_MULT)
    frame.set_alpha(a)

    x0, y0 = apply_expand_xy(fx.x, fx.y, W, H, expand)
    overlay.blit(frame, (x0 - frame.get_width() / 2, y0 - frame.get_height() / 2))
