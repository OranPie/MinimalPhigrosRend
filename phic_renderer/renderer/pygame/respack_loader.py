from __future__ import annotations

import os
import pygame

from typing import Any

from ...respack import Respack, load_respack_info


def _maybe_convert_alpha(surf: pygame.Surface) -> pygame.Surface:
    try:
        if pygame.display.get_surface() is not None:
            return surf.convert_alpha()
    except:
        pass
    return surf

def _parse_hex_rgba(v: Any, default: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
    if v is None:
        return default
    try:
        if isinstance(v, int):
            n = v
        else:
            s = str(v).strip()
            n = int(s, 0)
        if n <= 0xFFFFFF:
            r = (n >> 16) & 255
            g = (n >> 8) & 255
            b = n & 255
            a = 255
            return r, g, b, a
        a = (n >> 24) & 255
        r = (n >> 16) & 255
        g = (n >> 8) & 255
        b = n & 255
        return r, g, b, a
    except:
        return default


def load_respack(zip_path: str, *, audio: Any) -> Respack:
    tmpdir, info = load_respack_info(zip_path)

    def p(name: str) -> str:
        return os.path.join(tmpdir.name, name)

    required_imgs = [
        "click.png", "drag.png", "flick.png", "hold.png",
        "click_mh.png", "drag_mh.png", "flick_mh.png", "hold_mh.png",
        "hit_fx.png",
    ]
    img = {}
    for fn in required_imgs:
        img[fn] = _maybe_convert_alpha(pygame.image.load(p(fn)))

    sfx = {}
    for fn, key in [("click.ogg", "click"), ("drag.ogg", "drag"), ("flick.ogg", "flick")]:
        fp = p(fn)
        if os.path.exists(fp):
            try:
                sfx[key] = audio.load_sound(fp)
            except:
                pass

    hitfx_frames = info.get("hitFx", [5, 6])
    hitfx_duration = float(info.get("hitFxDuration", 0.5))
    hitfx_scale = float(info.get("hitFxScale", 1.0))
    hitfx_rotate = bool(info.get("hitFxRotate", False))
    hitfx_tinted = bool(info.get("hitFxTinted", True))

    ha = info.get("holdAtlas", [50, 50])
    hamh = info.get("holdAtlasMH", ha)

    hold_tail_h = int(ha[0])
    hold_head_h = int(ha[1])
    hold_tail_h_mh = int(hamh[0])
    hold_head_h_mh = int(hamh[1])

    hold_repeat = bool(info.get("holdRepeat", False))
    hold_compact = bool(info.get("holdCompact", False))
    hold_keep_head = bool(info.get("holdKeepHead", False))

    hide_particles = bool(info.get("hideParticles", False))

    judge_colors = {
        "PERFECT": _parse_hex_rgba(info.get("colorPerfect", None), (255, 255, 255, 255)),
        "GOOD": _parse_hex_rgba(info.get("colorGood", None), (180, 220, 255, 255)),
        "BAD": _parse_hex_rgba(info.get("colorBad", None), (255, 180, 180, 255)),
        "MISS": _parse_hex_rgba(info.get("colorMiss", None), (200, 200, 200, 255)),
    }

    return Respack(
        tmpdir=tmpdir,
        info=info,
        img=img,
        sfx=sfx,
        hitfx_sheet=img["hit_fx.png"],
        hitfx_frames_xy=(int(hitfx_frames[0]), int(hitfx_frames[1])),
        hitfx_duration=hitfx_duration,
        hitfx_scale=hitfx_scale,
        hitfx_rotate=hitfx_rotate,
        hitfx_tinted=hitfx_tinted,
        hold_tail_h=hold_tail_h,
        hold_head_h=hold_head_h,
        hold_tail_h_mh=hold_tail_h_mh,
        hold_head_h_mh=hold_head_h_mh,
        hold_repeat=hold_repeat,
        hold_compact=hold_compact,
        hold_keep_head=hold_keep_head,
        hide_particles=hide_particles,
        judge_colors=judge_colors,
    )
