from __future__ import annotations

import os
import re
import zipfile
import tempfile
import shutil
from dataclasses import dataclass
from typing import Dict, Any, Tuple

import pygame

@dataclass
class Respack:
    tmpdir: tempfile.TemporaryDirectory
    info: Dict[str, Any]
    img: Dict[str, pygame.Surface]
    sfx: Dict[str, pygame.mixer.Sound]
    hitfx_sheet: pygame.Surface
    hitfx_frames_xy: Tuple[int, int]
    hitfx_duration: float
    hitfx_scale: float
    hitfx_rotate: bool
    hitfx_tinted: bool
    hold_tail_h: int
    hold_head_h: int
    hold_tail_h_mh: int
    hold_head_h_mh: int
    hold_repeat: bool
    hold_compact: bool
    hold_keep_head: bool
    hide_particles: bool
    judge_colors: Dict[str, Tuple[int, int, int, int]]


def _parse_hex_rgba(v: Any, default: Tuple[int, int, int, int]) -> Tuple[int, int, int, int]:
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
        # respack commonly uses AARRGGBB
        a = (n >> 24) & 255
        r = (n >> 16) & 255
        g = (n >> 8) & 255
        b = n & 255
        return r, g, b, a
    except:
        return default

def _parse_info_yml_minimal(text: str) -> Dict[str, Any]:
    """
    Minimal YAML parser: supports key: value and key: [a,b] forms.
    Sufficient for reading name/author/hitFx/hitFxDuration/hitFxScale/hitFxRotate/hitFxTinted.
    """
    out: Dict[str, Any] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if ":" not in line:
            continue
        k, v = line.split(":", 1)
        k = k.strip()
        v = v.strip().strip('"').strip("'")
        if v.startswith("[") and v.endswith("]"):
            inside = v[1:-1].strip()
            if inside:
                parts = [p.strip() for p in inside.split(",")]
                arr = []
                for p in parts:
                    try:
                        arr.append(int(p))
                    except:
                        try:
                            arr.append(float(p))
                        except:
                            arr.append(p.strip('"').strip("'"))
                out[k] = arr
            else:
                out[k] = []
        else:
            # bool / number
            if v.lower() in ("true", "false"):
                out[k] = (v.lower() == "true")
            else:
                try:
                    out[k] = int(v)
                except:
                    try:
                        out[k] = float(v)
                    except:
                        out[k] = v
    return out

def load_respack(zip_path: str) -> Respack:
    tmpdir = tempfile.TemporaryDirectory()
    with zipfile.ZipFile(zip_path, "r") as z:
        z.extractall(tmpdir.name)

    def p(name: str) -> str:
        return os.path.join(tmpdir.name, name)

    # info.yml
    info_path = p("info.yml")
    with open(info_path, "r", encoding="utf-8") as f:
        info = _parse_info_yml_minimal(f.read())

    # required images (we load mh too but minimal renderer doesn't distinguish simultaneous press)
    required_imgs = [
        "click.png", "drag.png", "flick.png", "hold.png",
        "click_mh.png", "drag_mh.png", "flick_mh.png", "hold_mh.png",
        "hit_fx.png",
    ]
    img: Dict[str, pygame.Surface] = {}
    for fn in required_imgs:
        img[fn] = pygame.image.load(p(fn)).convert_alpha()

    # optional sfx
    sfx: Dict[str, pygame.mixer.Sound] = {}
    for fn, key in [("click.ogg","click"), ("drag.ogg","drag"), ("flick.ogg","flick")]:
        fp = p(fn)
        if os.path.exists(fp):
            sfx[key] = pygame.mixer.Sound(fp)

    hitfx_frames = info.get("hitFx", [5, 6])  # [w,h] frames
    hitfx_duration = float(info.get("hitFxDuration", 0.5))  # seconds
    hitfx_scale = float(info.get("hitFxScale", 1.0))
    hitfx_rotate = bool(info.get("hitFxRotate", False))
    hitfx_tinted = bool(info.get("hitFxTinted", True))

    # holdAtlas / holdAtlasMH: [tailHeight, headHeight]
    ha = info.get("holdAtlas", [50, 50])
    hamh = info.get("holdAtlasMH", ha)

    hold_tail_h = int(ha[0]); hold_head_h = int(ha[1])
    hold_tail_h_mh = int(hamh[0]); hold_head_h_mh = int(hamh[1])

    hold_repeat = bool(info.get("holdRepeat", False))     # optional
    hold_compact = bool(info.get("holdCompact", False))   # optional
    hold_keep_head = bool(info.get("holdKeepHead", False))# optional

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
        judge_colors=judge_colors
    )


