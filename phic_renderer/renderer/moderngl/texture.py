from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Optional, Tuple


@dataclass
class GLTexture:
    tex: Any
    size: Tuple[int, int]


def load_texture_rgba(ctx: Any, path: str, *, flip_y: bool = True) -> GLTexture:
    try:
        from PIL import Image  # type: ignore
    except:
        raise SystemExit(
            "ModernGL texture loading requires Pillow. Install it: pip install pillow"
        )

    img = Image.open(str(path)).convert("RGBA")
    if flip_y:
        img = img.transpose(Image.FLIP_TOP_BOTTOM)

    w, h = img.size
    tex = ctx.texture((w, h), components=4, data=img.tobytes())
    tex.filter = (ctx.LINEAR, ctx.LINEAR)
    tex.repeat_x = False
    tex.repeat_y = False
    return GLTexture(tex=tex, size=(w, h))


def texture_from_pil_image(ctx: Any, img: Any, *, flip_y: bool = True) -> GLTexture:
    try:
        from PIL import Image  # type: ignore
    except:
        raise SystemExit(
            "ModernGL texture creation requires Pillow. Install it: pip install pillow"
        )

    if img is None:
        raise ValueError("img is None")
    img = img.convert("RGBA")
    if flip_y:
        img = img.transpose(Image.FLIP_TOP_BOTTOM)
    w, h = img.size
    tex = ctx.texture((w, h), components=4, data=img.tobytes())
    tex.filter = (ctx.LINEAR, ctx.LINEAR)
    tex.repeat_x = False
    tex.repeat_y = False
    return GLTexture(tex=tex, size=(w, h))
