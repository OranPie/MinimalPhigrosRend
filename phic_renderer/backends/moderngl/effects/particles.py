"""
Particle burst system for ModernGL backend.

Renders particle effects for note hits using CPU-side physics
and GPU-side rendering with additive blending.
"""

from __future__ import annotations

from typing import Any, List

from ....engine.effects import ParticleBurst
from ....core.fx import prune_particles
from ....math.util import apply_expand_xy, rect_corners
from ..renderer2d import draw_quad_pts


class ParticleSystem:
    """
    Particle burst system with CPU physics and GPU rendering.

    Maintains a list of active particle bursts and renders them
    with additive blending for a glowing effect.
    """

    def __init__(self, ctx: Any):
        """
        Initialize particle system.

        Args:
            ctx: ModernGL context
        """
        self.ctx = ctx
        self._particles: List[ParticleBurst] = []

    def add_burst(self, x: float, y: float, now_tick: int, duration: int, color: tuple):
        """
        Add a particle burst at the specified position.

        Args:
            x: World X position
            y: World Y position
            now_tick: Current time in milliseconds
            duration: Burst duration in milliseconds
            color: RGBA color tuple (0-255)
        """
        self._particles.append(ParticleBurst(x, y, now_tick, duration, color))

    def update(self, now_tick: int):
        """
        Update particle system, removing expired bursts.

        Args:
            now_tick: Current time in milliseconds
        """
        self._particles[:] = prune_particles(list(self._particles), int(now_tick))

    def render(self, window_size: tuple, expand: float, now_tick: int, respack: Any = None):
        """
        Render all active particle bursts.

        Args:
            window_size: (width, height) tuple
            expand: Expand factor for coordinate transformation
            now_tick: Current time in milliseconds
            respack: Resource pack (optional, for hide_particles flag)
        """
        # Check if particles are disabled
        if respack is not None and bool(getattr(respack, "hide_particles", False)):
            return

        if not self._particles:
            return

        if now_tick <= 0:
            return

        W, H = window_size

        # Normalize expand factor
        if expand <= 1.000001:
            expand = 1.0

        # Enable additive blending for glow effect
        try:
            import moderngl  # type: ignore

            self.ctx.enable(moderngl.BLEND)
            old_blend = getattr(self.ctx, "blend_func", None)
            self.ctx.blend_func = (moderngl.SRC_ALPHA, moderngl.ONE)
        except:
            old_blend = None

        # Render each particle burst
        for burst in self._particles:
            try:
                particles = burst.get_particles(now_tick)
            except:
                continue

            for particle in particles:
                try:
                    # Transform position with expand
                    xq, yq = apply_expand_xy(
                        float(particle["x"]),
                        float(particle["y"]),
                        W, H, expand
                    )

                    # Scale size inversely with expand
                    sz = max(1.0, float(particle["size"]) / float(expand))

                    # Get color
                    c = particle["color"]

                    # Calculate quad corners
                    pts = rect_corners(float(xq), float(yq), float(sz), float(sz), 0.0)

                    # Draw colored quad
                    draw_quad_pts(pts, (int(c[0]), int(c[1]), int(c[2]), int(c[3])))
                except:
                    pass

        # Restore previous blend mode
        if old_blend is not None:
            try:
                self.ctx.blend_func = old_blend
            except:
                pass

    def clear(self):
        """Clear all particles."""
        self._particles.clear()

    def get_count(self) -> int:
        """Get number of active particle bursts."""
        return len(self._particles)
