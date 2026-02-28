"""
Line rendering for ModernGL backend.

Renders judgment lines as rotated rectangles with optional text overlay
and multicolor support.
"""

from __future__ import annotations

import math
from typing import Any, Callable

from ....engine.kinematics import eval_line_state
from ....math.util import apply_expand_xy


class LineRenderer:
    """
    Renders judgment lines with text overlay support.

    Lines are drawn as rotated rectangles. Supports scale_x/scale_y
    properties and optional text rendering on lines.
    """

    def __init__(self, ctx: Any, window_size: tuple, font_cache: Any):
        """
        Initialize line renderer.

        Args:
            ctx: ModernGL context
            window_size: (width, height) tuple
            font_cache: FontCache for text rendering
        """
        self.ctx = ctx
        self.window_size = window_size
        self.font_cache = font_cache

    def render_lines(
        self,
        lines: list,
        t: float,
        expand: float,
        draw_quad_fn: Callable,
        draw_textured_quad_fn: Callable,
        args: Any = None,
    ):
        """
        Render all judgment lines.

        Args:
            lines: List of RuntimeLine objects
            t: Current time in seconds
            expand: Expand factor for coordinates
            draw_quad_fn: Function to draw colored quads
            draw_textured_quad_fn: Function to draw textured quads
            args: Additional arguments for rendering options
        """
        W, H = self.window_size
        line_len = float(6.75 * W)
        line_w = max(1.0, 4.0 / float(expand))

        for ln in lines:
            lx, ly, lr, la01, _sc, _la_raw = eval_line_state(ln, t)

            # Skip invisible lines
            if la01 <= 1e-6:
                continue

            # Render text on line if present
            if getattr(ln, "text", None) is not None:
                self._render_line_text(
                    ln, t, lx, ly, la01, W, H, expand,
                    draw_textured_quad_fn, args
                )
                continue

            # Calculate scale factors
            sx = 1.0
            sy = 1.0
            try:
                if getattr(ln, "scale_x", None) is not None:
                    sx = float(ln.scale_x.eval(t) if hasattr(ln.scale_x, "eval") else 1.0)
            except:
                sx = 1.0
            try:
                if getattr(ln, "scale_y", None) is not None:
                    sy = float(ln.scale_y.eval(t) if hasattr(ln.scale_y, "eval") else 1.0)
            except:
                sy = 1.0

            if sx <= 1e-6:
                sx = 1.0
            if sy <= 1e-6:
                sy = 1.0

            # Calculate line geometry
            tx, ty = math.cos(lr), math.sin(lr)
            nx, ny = -ty, tx
            hl = (line_len * sx) * 0.5
            x0, y0 = lx - tx * hl, ly - ty * hl
            x1, y1 = lx + tx * hl, ly + ty * hl
            hw = (line_w * sy) * 0.5

            # Line quad corners
            q = [
                (x0 + nx * hw, y0 + ny * hw),
                (x1 + nx * hw, y1 + ny * hw),
                (x1 - nx * hw, y1 - ny * hw),
                (x0 - nx * hw, y0 - ny * hw),
            ]

            # Apply expand transformation
            q = [apply_expand_xy(px, py, W, H, expand) for (px, py) in q]

            # Determine color
            if args and getattr(args, "multicolor_lines", False):
                if getattr(ln, "color", None) is not None:
                    try:
                        rr, gg, bb = ln.color.eval(t)
                    except:
                        rr, gg, bb = ln.color_rgb
                else:
                    rr, gg, bb = ln.color_rgb
            else:
                rr, gg, bb = (255, 255, 255)

            # Draw line
            draw_quad_fn(q, (int(rr), int(gg), int(bb), int(255 * la01)))

    def _render_line_text(
        self,
        ln: Any,
        t: float,
        lx: float,
        ly: float,
        la01: float,
        W: int,
        H: int,
        expand: float,
        draw_textured_quad_fn: Callable,
        args: Any,
    ):
        """Render text overlay on line."""
        try:
            s = str(ln.text.eval(t) if hasattr(ln.text, "eval") else "")
        except:
            s = ""

        if not s:
            return

        # Determine text color
        rr, gg, bb = (255, 255, 255)
        try:
            if args and getattr(args, "multicolor_lines", False) and getattr(ln, "color", None) is not None:
                rr, gg, bb = ln.color.eval(t)
        except:
            rr, gg, bb = (255, 255, 255)

        # Calculate font size
        try:
            font_mul = float(getattr(args, "font_size_multiplier", 1.0) or 1.0)
        except:
            font_mul = 1.0
        if font_mul <= 1e-9:
            font_mul = 1.0
        font_size = max(1, int(round(16 * float(font_mul))))

        # Get font path
        font_path = getattr(args, "font_path", None) if args else None

        # Render text to texture
        tex = self.font_cache.get_text_texture(s, font_path=font_path, font_size=font_size)
        if tex is None:
            return

        # Position text at line position
        x0, y0 = apply_expand_xy(lx, ly, W, H, expand)
        tw, th = tex.size

        # Draw textured quad
        draw_textured_quad_fn(
            tex=tex,
            x0=float(x0),
            y0=float(y0),
            x1=float(x0 + tw),
            y1=float(y0 + th),
            rgba=(int(rr), int(gg), int(bb), int(255 * la01)),
        )
