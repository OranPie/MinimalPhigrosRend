"""
Frame renderer coordinator for ModernGL backend.

Orchestrates all rendering subsystems: background, lines, notes,
particles, hit effects, and UI overlays.
"""

from __future__ import annotations

from typing import Any, Optional

from ..sprite import draw_textured_quad, draw_textured_quad_pts
from ..renderer2d import draw_quad_pts as draw_quad_pts_r2d
from ....math.util import clamp


class FrameRenderer:
    """
    High-level frame rendering coordinator.

    Manages rendering pipeline: background → lines → notes → effects → UI.
    Delegates to specialized renderers for each subsystem.
    """

    def __init__(
        self,
        ctx: Any,
        sprite_program: Any,
        r2d: Any,
        window_size: tuple,
        line_renderer: Any,
        note_renderer: Any,
        particle_system: Any,
        hitfx_manager: Any,
    ):
        """
        Initialize frame renderer.

        Args:
            ctx: ModernGL context
            sprite_program: Sprite rendering program
            r2d: 2D geometry renderer
            window_size: (width, height) tuple
            line_renderer: LineRenderer for judgment lines
            note_renderer: NoteRenderer for notes
            particle_system: ParticleSystem for particle effects
            hitfx_manager: HitEffectManager for hit feedback
        """
        self.ctx = ctx
        self.sprite = sprite_program
        self.r2d = r2d
        self.window_size = window_size

        self.line_renderer = line_renderer
        self.note_renderer = note_renderer
        self.particle_system = particle_system
        self.hitfx_manager = hitfx_manager

    def render_frame(
        self,
        t: float,
        render_ctx: dict,
        states: list,
        args: Any = None,
    ) -> int:
        """
        Render a complete frame.

        Args:
            t: Current time in seconds
            render_ctx: Rendering context dictionary
            states: List of NoteState objects
            args: Additional rendering arguments

        Returns:
            Number of notes rendered
        """
        self.r2d.begin_frame()

        W, H = self.window_size

        # Step 1: Render background
        self._render_background(render_ctx, W, H, args)

        # Step 2: Get rendering data
        lines = render_ctx.get("lines") or []
        expand = float(render_ctx.get("expand") or getattr(args, "expand", 1.0) or 1.0)
        if expand <= 1.000001:
            expand = 1.0

        respack = render_ctx.get("respack", None)

        # Step 3: Render judgment lines
        def draw_quad_wrapper(pts, rgba):
            r, g, b, a = rgba
            cf = (r / 255.0, g / 255.0, b / 255.0, a / 255.0)
            (x0, y0), (x1, y1), (x2, y2), (x3, y3) = pts
            import struct
            verts = [
                (x0, y0, *cf),
                (x1, y1, *cf),
                (x2, y2, *cf),
                (x0, y0, *cf),
                (x2, y2, *cf),
                (x3, y3, *cf),
            ]
            data = b"".join(struct.pack("6f", *v) for v in verts)
            self.r2d.draw_triangles(data, 6)

        def draw_textured_quad_wrapper(tex, x0, y0, x1, y1, rgba):
            draw_textured_quad(
                ctx=self.ctx,
                sp=self.sprite,
                tex=tex,
                window_size=self.window_size,
                x0=x0,
                y0=y0,
                x1=x1,
                y1=y1,
                rgba=rgba,
            )

        self.line_renderer.render_lines(
            lines, t, expand,
            draw_quad_wrapper,
            draw_textured_quad_wrapper,
            args
        )

        # Step 4: Render notes
        def draw_textured_quad_pts_wrapper(tex, pts, rgba):
            draw_textured_quad_pts(
                ctx=self.ctx,
                sp=self.sprite,
                tex=tex,
                window_size=self.window_size,
                pts=pts,
                rgba=rgba,
            )

        # Pass respack through args for hold rendering
        if args:
            args._respack = respack

        note_render_count = self.note_renderer.render_notes(
            states, lines, t, expand, respack,
            draw_quad_wrapper,
            draw_textured_quad_pts_wrapper,
            args
        )

        # Step 5: Render hit effects
        self.hitfx_manager.render(
            t, self.window_size, expand, self.sprite, respack, args
        )

        return note_render_count

    def _render_background(self, render_ctx: dict, W: int, H: int, args: Any):
        """Render background image and dim overlay."""
        # Background image
        bg_tex = render_ctx.get("bg_tex", None)
        if bg_tex is not None:
            try:
                draw_textured_quad(
                    ctx=self.ctx,
                    sp=self.sprite,
                    tex=bg_tex,
                    window_size=self.window_size,
                    x0=0.0,
                    y0=0.0,
                    x1=float(W),
                    y1=float(H),
                    rgba=(255, 255, 255, 255),
                )
            except:
                pass

        # Dim overlay
        try:
            dim = render_ctx.get("bg_dim_alpha", None)
            if dim is None:
                dim = clamp(getattr(args, "bg_dim", 120), 0, 255) if args else 120
            dim = int(dim)
        except:
            dim = 120

        if dim > 0:
            try:
                draw_quad_pts_r2d(
                    [(0.0, 0.0), (float(W), 0.0), (float(W), float(H)), (0.0, float(H))],
                    (0, 0, 0, int(dim)),
                )
            except:
                pass

    def render_particles(self, now_tick: int, expand: float, respack: Any = None):
        """
        Render particle effects.

        Args:
            now_tick: Current time in milliseconds
            expand: Expand factor
            respack: Resource pack (for hide_particles flag)
        """
        self.particle_system.render(self.window_size, expand, now_tick, respack)

    def update_window_size(self, width: int, height: int):
        """
        Update window size for all renderers.

        Args:
            width: New window width
            height: New window height
        """
        self.window_size = (width, height)
