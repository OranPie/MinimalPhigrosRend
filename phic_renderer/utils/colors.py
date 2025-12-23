from __future__ import annotations

from ..math.util import lerp

def tint(rgb, tint_rgb, amt=0.35):
    r = int(lerp(rgb[0], tint_rgb[0], amt))
    g = int(lerp(rgb[1], tint_rgb[1], amt))
    b = int(lerp(rgb[2], tint_rgb[2], amt))
    return (r, g, b)
