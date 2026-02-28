"""
Hit feedback effects for ModernGL backend.

Renders expanding circles or sprite-sheet animations when notes are hit.
Supports variant hit effects (e.g., "good" vs "perfect").
"""

from __future__ import annotations

from typing import Any, List

from ....engine.effects import HitFX
from ....math.util import apply_expand_xy, rect_corners, clamp
from ..renderer2d import draw_quad_pts
from ..sprite import draw_textured_quad_pts_uv


class HitEffectManager:
    """
    Manager for hit feedback effects.

    Handles both default expanding circles (no respack) and
    sprite-sheet animations (with respack).
    """

    def __init__(self, ctx: Any):
        """
        Initialize hit effect manager.

        Args:
            ctx: ModernGL context
        """
        self.ctx = ctx
        self._hitfx: List[HitFX] = []

    def add_effect(self, x: float, y: float, t0: float, rgba: tuple, rot: float = 0.0, variant: str = ""):
        """
        Add a hit effect at the specified position.

        Args:
            x: World X position
            y: World Y position
            t0: Start time in seconds
            rgba: RGBA color tuple (0-255)
            rot: Rotation angle in radians (for respack effects)
            variant: Effect variant ("good" for good hits, "" for perfect)
        """
        self._hitfx.append(HitFX(x=x, y=y, t0=float(t0), rgba=rgba, rot=float(rot), variant=variant))

    def update(self, t: float, respack: Any = None):
        """
        Update hit effects, removing expired ones.

        Args:
            t: Current time in seconds
            respack: Resource pack (optional, for duration)
        """
        dur = 0.18
        if respack is not None:
            try:
                dur = max(1e-6, float(respack.hitfx_duration))
            except:
                dur = 0.18

        # Remove expired effects
        self._hitfx[:] = [fx for fx in self._hitfx if (t - float(fx.t0)) <= float(dur)]

    def render(self, t: float, window_size: tuple, expand: float, sprite_program: Any, respack: Any = None, args: Any = None):
        """
        Render all active hit effects.

        Args:
            t: Current time in seconds
            window_size: (width, height) tuple
            expand: Expand factor for coordinate transformation
            sprite_program: Sprite rendering program
            respack: Resource pack (optional, for sprite sheets)
            args: Additional arguments (for scale multiplier)
        """
        if not self._hitfx:
            return

        W, H = window_size

        for fx in self._hitfx:
            age = float(t) - float(fx.t0)

            # Default effect (no respack): expanding circle
            if respack is None:
                self._render_default_effect(fx, age, W, H, expand)
                continue

            # Sprite-sheet effect (with respack)
            dur = 0.18
            try:
                dur = max(1e-6, float(respack.hitfx_duration))
            except:
                dur = 0.18

            if age < 0.0 or age > float(dur):
                continue

            self._render_spritesheet_effect(fx, age, dur, W, H, expand, sprite_program, respack, args)

    def _render_default_effect(self, fx: HitFX, age: float, W: int, H: int, expand: float):
        """
        Render default expanding circle effect.

        Args:
            fx: Hit effect data
            age: Effect age in seconds
            W: Window width
            H: Window height
            expand: Expand factor
        """
        if age < 0.0 or age > 0.18:
            return

        # Expanding radius
        r = float(10.0 + 140.0 * age)

        # Fading alpha
        a = int(255 * (1.0 - age / 0.18))

        # Transform position
        rr, gg, bb, _aa = fx.rgba
        x0, y0 = apply_expand_xy(float(fx.x), float(fx.y), W, H, expand)

        # Draw colored quad
        pts = rect_corners(float(x0), float(y0), float(r), float(r), 0.0)
        draw_quad_pts(pts, (int(rr), int(gg), int(bb), int(a)))

    def _render_spritesheet_effect(self, fx: HitFX, age: float, dur: float, W: int, H: int,
                                   expand: float, sprite_program: Any, respack: Any, args: Any):
        """
        Render sprite-sheet animation effect.

        Args:
            fx: Hit effect data
            age: Effect age in seconds
            dur: Effect duration in seconds
            W: Window width
            H: Window height
            expand: Expand factor
            sprite_program: Sprite rendering program
            respack: Resource pack with sprite sheets
            args: Additional arguments
        """
        # Get sprite sheet dimensions
        fw, fh = respack.hitfx_frames_xy
        sheet = respack.hitfx_sheet

        # Check for variant sprite sheet (e.g., "good" hits)
        try:
            if str(getattr(fx, "variant", "") or "").lower() == "good":
                sheet2 = getattr(respack, "hitfx_sheet_good", None)
                if sheet2 is not None:
                    sheet = sheet2
        except Exception:
            pass

        # Calculate cell dimensions
        sw, sh = sheet.size
        cell_w = float(sw) / max(1, int(fw))
        cell_h = float(sh) / max(1, int(fh))

        # Calculate current frame index
        p = clamp(age / float(dur), 0.0, 0.999999)
        idx = int(p * float(int(fw) * int(fh)))
        ix = int(idx % int(fw))
        iy = int(idx // int(fw))

        # Calculate scale
        try:
            sc = (float(respack.hitfx_scale) * float(getattr(args, "hitfx_scale_mul", 1.0) or 1.0)) / float(expand)
        except:
            sc = 1.0 / float(expand)
        if sc <= 1e-6:
            sc = 1.0 / float(expand)

        # Transform position
        x0, y0 = apply_expand_xy(float(fx.x), float(fx.y), W, H, expand)

        # Apply rotation if enabled
        ang = float(fx.rot) if bool(getattr(respack, "hitfx_rotate", False)) else 0.0

        # Calculate quad corners
        pts = rect_corners(float(x0), float(y0), float(cell_w * sc), float(cell_h * sc), float(ang))

        # Calculate UV coordinates for current frame
        u0 = float(ix) / float(max(1, int(fw)))
        v0 = float(iy) / float(max(1, int(fh)))
        u1 = float(ix + 1) / float(max(1, int(fw)))
        v1 = float(iy + 1) / float(max(1, int(fh)))

        # Apply color tinting
        r, g, b, a = fx.rgba
        if (not bool(getattr(respack, "hitfx_tinted", True))) and (int(r), int(g), int(b)) == (255, 255, 255):
            r, g, b = (255, 255, 255)

        # Draw textured quad
        draw_textured_quad_pts_uv(
            ctx=self.ctx,
            sp=sprite_program,
            tex=sheet,
            window_size=(W, H),
            pts=pts,
            uv0=(u0, v0),
            uv1=(u1, v1),
            rgba=(int(r), int(g), int(b), int(a)),
        )

    def clear(self):
        """Clear all hit effects."""
        self._hitfx.clear()

    def get_count(self) -> int:
        """Get number of active hit effects."""
        return len(self._hitfx)
