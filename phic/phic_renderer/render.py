from __future__ import annotations

import math
from typing import Tuple

import pygame

from . import state
from .util import rotate_vec, rect_corners, draw_poly_rgba, draw_poly_outline_rgba

NOTE_TYPE_COLORS = {
    1: (255, 220, 120),  # tap
    2: (140, 240, 255),  # drag
    3: (120, 200, 255),  # hold
    4: (255, 140, 220),  # flick
}

def tint(rgb, tint_rgb, amt=0.35):
    r = int(lerp(rgb[0], tint_rgb[0], amt))
    g = int(lerp(rgb[1], tint_rgb[1], amt))
    b = int(lerp(rgb[2], tint_rgb[2], amt))
    return (r, g, b)


def draw_hold_3slice(
    overlay: pygame.Surface,
    head_xy: Tuple[float, float],
    tail_xy: Tuple[float, float],
    line_rot: float,
    alpha01: float,
    line_rgb: Tuple[int,int,int],
    note_rgb: Tuple[int,int,int],
    size_scale: float,
    mh: bool,
    hold_body_w: int,
    draw_outline: bool = True,
    outline_width: int = 2
):
    rp = state.respack
    respack = rp  # legacy alias
    """
    Use respack's hold.png / hold_mh.png for three-segment slicing:
    Image from top to bottom: tail / middle / head; holdAtlas specifies tailHeight, headHeight (pixels).

    head_xy: end close to judgment line
    tail_xy: end far from judgment line
    """
    if not respack:
        return

    img_key = "hold_mh.png" if mh else "hold.png"
    img = respack.img[img_key]
    iw, ih = img.get_width(), img.get_height()

    tail_h = respack.hold_tail_h_mh if mh else respack.hold_tail_h
    head_h = respack.hold_head_h_mh if mh else respack.hold_head_h
    mid_h = max(1, ih - tail_h - head_h)

    # slices (source)
    tail_src = img.subsurface((0, 0, iw, tail_h))
    mid_src  = img.subsurface((0, tail_h, iw, mid_h))
    head_src = img.subsurface((0, tail_h + mid_h, iw, head_h))

    # axis from head -> tail
    vx = tail_xy[0] - head_xy[0]
    vy = tail_xy[1] - head_xy[1]
    length = math.hypot(vx, vy)
    if length < 1e-3:
        return
    ang = math.atan2(vy, vx)

    # target width: follow your note width scale (same spirit as click/drag sprites)
    # use hold_body_w * size_scale as visual width baseline (consistent with before)
    target_w = max(2, int(hold_body_w * size_scale))
    scale = target_w / max(1, iw)

    tail_len = tail_h * scale
    head_len = head_h * scale

    # compact: mid full length, then overlay head/tail
    # non-compact: tail + mid + head = total length (if insufficient, proportionally compress head/tail)
    if respack.hold_compact:
        mid_len = max(1.0, length)
    else:
        if (tail_len + head_len) > length:
            # squash ends proportionally to fit
            s = length / max(1e-6, (tail_len + head_len))
            tail_len *= s
            head_len *= s
            mid_len = 1.0
        else:
            mid_len = max(1.0, length - tail_len - head_len)

    # build a local surface aligned along +Y (so we can blit slices easily), then rotate to ang
    out_w = target_w
    out_h = int(max(2, (tail_len + mid_len + head_len) if not respack.hold_compact else length))
    surf = pygame.Surface((out_w, out_h), pygame.SRCALPHA)

    # helper: scale slice to (out_w, seg_len)
    def blit_scaled(src: pygame.Surface, y0: int, seg_len: float, repeat: bool):
        seg_h = max(1, int(seg_len))
        if not repeat:
            piece = pygame.transform.smoothscale(src, (out_w, seg_h))
            surf.blit(piece, (0, y0))
        else:
            # repeat along length: tile the mid slice (scaled to width, keep src aspect vertically)
            tile_h = max(1, int(src.get_height() * scale))
            tile = pygame.transform.smoothscale(src, (out_w, tile_h))
            yy = y0
            y_end = y0 + seg_h
            while yy < y_end:
                surf.blit(tile, (0, yy))
                yy += tile_h

    # layout from top to bottom on the surface: tail -> mid -> head
    # BUT our axis is head->tail; we want head at y=0 and tail at y=end.
    # So we place head first, then mid, then tail.
    y = 0
    blit_scaled(head_src, y, head_len, repeat=False)
    y += int(head_len)

    if respack.hold_compact:
        # mid spans whole length; then overlay head/tail at ends
        # mid fill:
        blit_scaled(mid_src, 0, length, repeat=respack.hold_repeat)
        # overlay head at top:
        blit_scaled(head_src, 0, head_len, repeat=False)
        # overlay tail at bottom:
        blit_scaled(tail_src, max(0, int(length - tail_len)), tail_len, repeat=False)
    else:
        blit_scaled(mid_src, y, mid_len, repeat=respack.hold_repeat)
        y += int(mid_len)
        blit_scaled(tail_src, y, tail_len, repeat=False)

    # alpha + tint
    a = int(255 * clamp(alpha01, 0.0, 1.0))
    surf.set_alpha(a)

    # rotate into world
    # surf is built aligned to +Y; rotate so +Y aligns to (head->tail) vector
    spr = pygame.transform.rotozoom(surf, (ang * 180.0 / math.pi - 90.0), 1.0)
    overlay.blit(spr, (head_xy[0] - spr.get_width()/2, head_xy[1] - spr.get_height()/2))

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


