"""
Hold note 3-slice rendering for ModernGL backend.

Implements GPU-based hold rendering with head/middle/tail sections,
supporting compact mode, texture repeating, outlines, and progress visualization.
"""

from __future__ import annotations

import math
from typing import Any, Optional

from ....math.util import clamp
from ..sprite import draw_textured_quad_pts_uv
from ..renderer2d import draw_quad_pts


class HoldRenderer:
    """
    3-slice hold renderer with GPU texture UV mapping.

    Splits hold texture into head, middle, and tail sections,
    rendering each with appropriate UV coordinates for length scaling.
    """

    def __init__(self, ctx: Any, sprite_program: Any, window_size: tuple):
        """
        Initialize hold renderer.

        Args:
            ctx: ModernGL context
            sprite_program: Sprite rendering program
            window_size: (width, height) tuple
        """
        self.ctx = ctx
        self.sprite = sprite_program
        self.window_size = window_size

    def draw_hold_3slice(
        self,
        *,
        head_xy: tuple[float, float],
        tail_xy: tuple[float, float],
        line_rot: float,
        alpha01: float,
        line_rgb: tuple[int, int, int],
        note_rgb: tuple[int, int, int],
        size_scale: float,
        mh: bool,
        hold_body_w: float,
        progress: Optional[float] = None,
        draw_outline: bool = False,
        outline_width: float = 2.0,
        respack: Any = None,
    ) -> None:
        """
        Draw a hold note using 3-slice texture mapping.

        Args:
            head_xy: Head position (start of hold)
            tail_xy: Tail position (end of hold)
            line_rot: Line rotation (unused in current implementation)
            alpha01: Alpha transparency (0.0-1.0)
            line_rgb: Line color for outline
            note_rgb: Note color for hold body
            size_scale: Size scaling factor
            mh: Multi-hit flag (uses hold_mh.png if True)
            hold_body_w: Base hold body width
            progress: Release progress (0.0-1.0) for visual feedback
            draw_outline: Whether to draw outline
            outline_width: Outline width in pixels
            respack: Resource pack with hold textures
        """
        if respack is None:
            return

        # Get hold texture
        try:
            tex = respack.img["hold_mh.png"] if mh else respack.img["hold.png"]
        except:
            return
        if tex is None:
            return

        # Calculate hold direction and length
        vx = float(tail_xy[0]) - float(head_xy[0])
        vy = float(tail_xy[1]) - float(head_xy[1])
        length = math.hypot(vx, vy)
        if length <= 1e-3:
            return

        # Unit vector along hold
        ux, uy = vx / length, vy / length
        # Normal vector (perpendicular)
        nx, ny = -uy, ux

        # Get texture dimensions and slice heights
        iw, ih = tex.size
        try:
            tail_h = int(respack.hold_tail_h_mh) if mh else int(respack.hold_tail_h)
            head_h = int(respack.hold_head_h_mh) if mh else int(respack.hold_head_h)
        except:
            tail_h, head_h = 50, 50

        tail_h = max(0, min(int(ih), int(tail_h)))
        head_h = max(0, min(int(ih), int(head_h)))
        mid_h = max(1, int(ih) - int(tail_h) - int(head_h))

        # Calculate target width and scale
        target_w = max(2.0, float(hold_body_w) * float(size_scale))
        scale = float(target_w) / max(1.0, float(iw))

        # Calculate geometric lengths for each section
        tail_len = float(tail_h) * scale
        head_len = float(head_h) * scale

        # Determine middle section length
        if bool(getattr(respack, "hold_compact", False)):
            # Compact mode: stretch middle across entire length
            mid_len = float(length)
        else:
            # Normal mode: fit head/mid/tail proportionally
            if (tail_len + head_len) > float(length):
                # Shrink head/tail to fit
                s = float(length) / max(1e-6, (tail_len + head_len))
                tail_len *= s
                head_len *= s
                mid_len = 1.0
            else:
                mid_len = max(1.0, float(length) - tail_len - head_len)

        # Calculate UV coordinates for each section
        u0, u1 = 0.0, 1.0
        denom = float(max(1, int(ih)))
        v_tail0 = 0.0
        v_tail1 = float(tail_h) / denom
        v_head0 = 1.0 - (float(head_h) / denom)
        v_head1 = 1.0
        v_mid0 = float(v_tail1)
        v_mid1 = float(v_head0)
        if float(v_mid1) < float(v_mid0):
            v_mid1 = float(v_mid0)

        # Calculate half-width and color
        hw = float(target_w) * 0.5
        r, g, b = (int(note_rgb[0]), int(note_rgb[1]), int(note_rgb[2]))
        a = int(255 * clamp(float(alpha01), 0.0, 1.0))
        if a <= 0:
            return
        rgba = (r, g, b, int(a))

        def _quad(a0: float, a1: float) -> list[tuple[float, float]]:
            """Generate quad corners for segment from a0 to a1 along hold."""
            sx = float(head_xy[0]) + ux * float(a0)
            sy = float(head_xy[1]) + uy * float(a0)
            ex = float(head_xy[0]) + ux * float(a1)
            ey = float(head_xy[1]) + uy * float(a1)
            return [
                (sx + nx * hw, sy + ny * hw),
                (sx - nx * hw, sy - ny * hw),
                (ex - nx * hw, ey - ny * hw),
                (ex + nx * hw, ey + ny * hw),
            ]

        # Progress visualization: crop texture to show release progress
        if progress is not None:
            try:
                p = clamp(float(progress), 0.0, 1.0)
            except:
                p = None
            if p is not None and p > 1e-6:
                keep = clamp(1.0 - float(p), 0.02, 1.0)
                try:
                    pts = _quad(0.0, float(length))
                    draw_textured_quad_pts_uv(
                        ctx=self.ctx,
                        sp=self.sprite,
                        tex=tex,
                        window_size=self.window_size,
                        pts=pts,
                        uv0=(u0, 0.0),
                        uv1=(u1, float(keep)),
                        rgba=rgba,
                    )
                    return
                except:
                    pass

        # Draw outline if enabled
        if draw_outline and outline_width > 0.0:
            self._draw_outline(head_xy, tail_xy, nx, ny, hw, outline_width, line_rgb, a)

        def _draw_mid_repeat(a_start: float, seg_len: float) -> None:
            """Draw middle section with optional tiling."""
            tile_len = float(mid_h) * scale
            if tile_len <= 1e-6:
                return

            if not bool(getattr(respack, "hold_repeat", False)):
                # Stretch middle section
                pts = _quad(a_start, a_start + float(seg_len))
                draw_textured_quad_pts_uv(
                    ctx=self.ctx,
                    sp=self.sprite,
                    tex=tex,
                    window_size=self.window_size,
                    pts=pts,
                    uv0=(u0, float(v_mid0)),
                    uv1=(u1, float(v_mid1)),
                    rgba=rgba,
                )
                return

            # Tile middle section
            pos = float(a_start)
            rem = float(seg_len)
            while rem > 1e-6:
                cur = min(float(tile_len), float(rem))
                v_end = float(v_mid0) + (float(v_mid1) - float(v_mid0)) * clamp(
                    cur / max(1e-6, tile_len), 0.0, 1.0
                )
                pts = _quad(pos, pos + cur)
                draw_textured_quad_pts_uv(
                    ctx=self.ctx,
                    sp=self.sprite,
                    tex=tex,
                    window_size=self.window_size,
                    pts=pts,
                    uv0=(u0, float(v_mid0)),
                    uv1=(u1, float(v_end)),
                    rgba=rgba,
                )
                pos += cur
                rem -= cur

        # Render hold sections
        if bool(getattr(respack, "hold_compact", False)):
            # Compact mode: middle fills entire length, head/tail overlay
            _draw_mid_repeat(0.0, float(length))
            if head_len > 1e-6:
                pts = _quad(0.0, float(head_len))
                draw_textured_quad_pts_uv(
                    ctx=self.ctx,
                    sp=self.sprite,
                    tex=tex,
                    window_size=self.window_size,
                    pts=pts,
                    uv0=(u0, float(v_head0)),
                    uv1=(u1, float(v_head1)),
                    rgba=rgba,
                )
            if tail_len > 1e-6:
                pts = _quad(max(0.0, float(length) - float(tail_len)), float(length))
                draw_textured_quad_pts_uv(
                    ctx=self.ctx,
                    sp=self.sprite,
                    tex=tex,
                    window_size=self.window_size,
                    pts=pts,
                    uv0=(u0, float(v_tail0)),
                    uv1=(u1, float(v_tail1)),
                    rgba=rgba,
                )
            return

        # Normal mode: head → middle → tail sequence
        if head_len > 1e-6:
            pts = _quad(0.0, float(head_len))
            draw_textured_quad_pts_uv(
                ctx=self.ctx,
                sp=self.sprite,
                tex=tex,
                window_size=self.window_size,
                pts=pts,
                uv0=(u0, float(v_head0)),
                uv1=(u1, float(v_head1)),
                rgba=rgba,
            )

        _draw_mid_repeat(float(head_len), float(mid_len))

        if tail_len > 1e-6:
            pts = _quad(float(head_len + mid_len), float(head_len + mid_len + tail_len))
            draw_textured_quad_pts_uv(
                ctx=self.ctx,
                sp=self.sprite,
                tex=tex,
                window_size=self.window_size,
                pts=pts,
                uv0=(u0, float(v_tail0)),
                uv1=(u1, float(v_tail1)),
                rgba=rgba,
            )

    def _draw_outline(self, head_xy, tail_xy, nx, ny, hw, outline_width, line_rgb, a):
        """Draw hold outline as 4 border quads."""
        try:
            ow = max(0.5, float(outline_width))
            hw_in = max(0.0, hw - ow)
            sx = float(head_xy[0])
            sy = float(head_xy[1])
            ex = float(tail_xy[0])
            ey = float(tail_xy[1])

            # Outer corners
            p0 = (sx + nx * hw, sy + ny * hw)
            p1 = (sx - nx * hw, sy - ny * hw)
            p2 = (ex - nx * hw, ey - ny * hw)
            p3 = (ex + nx * hw, ey + ny * hw)

            # Inner corners
            q0 = (sx + nx * hw_in, sy + ny * hw_in)
            q1 = (sx - nx * hw_in, sy - ny * hw_in)
            q2 = (ex - nx * hw_in, ey - ny * hw_in)
            q3 = (ex + nx * hw_in, ey + ny * hw_in)

            lr, lg, lb = (int(line_rgb[0]), int(line_rgb[1]), int(line_rgb[2]))
            rgba_o = (lr, lg, lb, int(a))

            # Draw 4 border quads
            draw_quad_pts([p0, q0, q3, p3], rgba_o)  # Top border
            draw_quad_pts([q1, p1, p2, q2], rgba_o)  # Bottom border
            draw_quad_pts([p0, p1, q1, q0], rgba_o)  # Left border
            draw_quad_pts([q3, q2, p2, p3], rgba_o)  # Right border
        except:
            pass
