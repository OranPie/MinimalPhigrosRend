from __future__ import annotations

import math
from typing import Optional, Tuple

import pygame

from ... import state
from ...math.util import clamp
from .draw import draw_poly_outline_rgba


def draw_hold_3slice(
    overlay: pygame.Surface,
    head_xy: Tuple[float, float],
    tail_xy: Tuple[float, float],
    line_rot: float,
    alpha01: float,
    line_rgb: Tuple[int, int, int],
    note_rgb: Tuple[int, int, int],
    size_scale: float,
    mh: bool,
    hold_body_w: int,
    progress: Optional[float] = None,
    draw_outline: bool = True,
    outline_width: int = 2,
):
    rp = state.respack
    respack = rp
    if not respack:
        return

    img_key = "hold_mh.png" if mh else "hold.png"
    img = respack.img[img_key]
    iw, ih = img.get_width(), img.get_height()

    tail_h = respack.hold_tail_h_mh if mh else respack.hold_tail_h
    head_h = respack.hold_head_h_mh if mh else respack.hold_head_h
    mid_h = max(1, ih - head_h - tail_h)

    head_src = img.subsurface((0, 0, iw, head_h))
    mid_src = img.subsurface((0, head_h, iw, mid_h))
    tail_src = img.subsurface((0, head_h + mid_h, iw, tail_h))
    
    vx = tail_xy[0] - head_xy[0]
    vy = tail_xy[1] - head_xy[1]
    length = math.hypot(vx, vy)
    if length < 1e-3:
        return
    ang = math.atan2(vy, vx)

    target_w = max(2, int(hold_body_w * size_scale))
    scale = target_w / max(1, iw)

    tail_len = tail_h * scale
    head_len = head_h * scale

    out_w = target_w
    out_h = int(max(2, length))
    surf = pygame.Surface((out_w, out_h), pygame.SRCALPHA)

    def _blit_mid(y0: int, seg_h: int, repeat: bool):
        if seg_h <= 0:
            return
        if not repeat:
            piece = pygame.transform.smoothscale(mid_src, (out_w, int(seg_h)))
            surf.blit(piece, (0, int(y0)))
            return
        tile_h = max(1, int(mid_src.get_height() * scale))
        tile = pygame.transform.smoothscale(mid_src, (out_w, tile_h))
        yy = int(y0)
        y_end = int(y0 + seg_h)
        while yy < y_end:
            surf.blit(tile, (0, yy))
            yy += tile_h

    try:
        head_piece = pygame.transform.smoothscale(head_src, (out_w, max(1, int(round(head_len)))))
    except:
        head_piece = head_src
    try:
        tail_piece = pygame.transform.smoothscale(tail_src, (out_w, max(1, int(round(tail_len)))))
    except:
        tail_piece = tail_src

    head_draw_h = min(int(out_h), int(head_piece.get_height()))
    tail_draw_h = min(int(out_h), int(tail_piece.get_height()))

    y0_mid = int(head_draw_h)
    y1_mid = int(out_h - tail_draw_h)
    mid_h_draw = int(max(0, y1_mid - y0_mid))
    _blit_mid(y0_mid, mid_h_draw, repeat=bool(getattr(respack, "hold_repeat", False)))

    if head_draw_h > 0:
        try:
            surf.blit(head_piece.subsurface((0, 0, out_w, head_draw_h)), (0, 0))
        except:
            surf.blit(head_piece, (0, 0))

    if tail_draw_h > 0:
        try:
            y_src = max(0, int(tail_piece.get_height() - tail_draw_h))
            crop = tail_piece.subsurface((0, int(y_src), out_w, tail_draw_h))
            surf.blit(crop, (0, int(out_h - tail_draw_h)))
        except:
            surf.blit(tail_piece, (0, int(out_h - tail_piece.get_height())))

    # During holding, allow sampling only the "tail side" portion of the texture and stretch it
    # back to the current geometric length. This makes the texture appear to be "consumed".
    if progress is not None:
        try:
            p = clamp(float(progress), 0.0, 1.0)
        except:
            p = None
        if p is not None and p > 1e-6:
            keep = clamp(1.0 - float(p), 0.02, 1.0)
            try:
                sample_h = max(2, int(round(float(out_h) * float(keep))))
                y0 = max(0, int(out_h) - int(sample_h))
                crop = surf.subsurface((0, int(y0), int(out_w), int(sample_h))).copy()
                surf = pygame.transform.smoothscale(crop, (int(out_w), int(out_h)))
            except:
                pass

    try:
        tr, tg, tb = note_rgb
        tint_s = pygame.Surface(surf.get_size(), pygame.SRCALPHA)
        tint_s.fill((int(tr), int(tg), int(tb), 255))
        surf.blit(tint_s, (0, 0), special_flags=pygame.BLEND_RGBA_MULT)
    except:
        pass

    a = int(255 * clamp(alpha01, 0.0, 1.0))
    surf.set_alpha(a)

    v = pygame.math.Vector2(float(vx), float(vy))
    v_len = float(v.length())
    if v_len < 1e-6:
        return
    v_m = pygame.math.Vector2(float(v.x), float(-v.y))
    base_m = pygame.math.Vector2(0.0, 1.0)
    rot_deg = float(base_m.angle_to(v_m))

    spr = pygame.transform.rotozoom(surf, rot_deg, 1.0)
    # Anchor: align the head end of the (rotated) sprite to head_xy.
    try:
        off = pygame.math.Vector2(0.0, float(out_h) * 0.5)
        off_m = pygame.math.Vector2(float(off.x), float(-off.y))
        off_m = off_m.rotate(rot_deg)
        off = pygame.math.Vector2(float(off_m.x), float(-off_m.y))
        cx = float(head_xy[0]) - float(off.x)
        cy = float(head_xy[1]) - float(off.y)
        rect = spr.get_rect(center=(cx, cy))
        overlay.blit(spr, rect.topleft)
    except:
        overlay.blit(spr, (head_xy[0] - spr.get_width() / 2, head_xy[1] - spr.get_height() / 2))

    if draw_outline:
        hw = target_w * 0.5
        nx, ny = -math.sin(ang), math.cos(ang)
        pts = [
            (head_xy[0] + nx * hw, head_xy[1] + ny * hw),
            (head_xy[0] - nx * hw, head_xy[1] - ny * hw),
            (tail_xy[0] - nx * hw, tail_xy[1] - ny * hw),
            (tail_xy[0] + nx * hw, tail_xy[1] + ny * hw),
        ]
        draw_poly_outline_rgba(overlay, pts, (*line_rgb, a), width=max(1, int(outline_width)))
