"""
Motion blur effect for ModernGL backend.

Implements multi-sample motion blur using additive blending.
Renders multiple frames at different time offsets and blends them together.
"""

from __future__ import annotations

from typing import Any, Callable

from ....math.util import clamp


class MotionBlurEffect:
    """
    GPU-based motion blur using multi-sample rendering.

    Renders multiple samples at different time offsets and combines
    them with additive blending for a smooth motion blur effect.
    """

    def __init__(self, ctx: Any):
        """
        Initialize motion blur effect.

        Args:
            ctx: ModernGL context
        """
        self.ctx = ctx
        self._old_blend_func = None

    def begin_blur(self):
        """
        Begin motion blur rendering with additive blending.

        Sets up OpenGL blend mode for accumulating multiple samples.
        """
        try:
            import moderngl  # type: ignore

            self.ctx.enable(moderngl.BLEND)
            self._old_blend_func = getattr(self.ctx, "blend_func", None)
            self.ctx.blend_func = (moderngl.ONE, moderngl.ONE)
        except:
            self._old_blend_func = None

    def end_blur(self):
        """
        End motion blur rendering and restore previous blend mode.
        """
        if self._old_blend_func is not None:
            try:
                self.ctx.blend_func = self._old_blend_func
            except:
                pass
            self._old_blend_func = None

    def render_with_blur(
        self,
        render_callback: Callable[[float, float], None],
        t_base: float,
        dt: float,
        chart_speed: float,
        samples: int,
        shutter: float,
    ):
        """
        Render scene with motion blur.

        Args:
            render_callback: Function to render scene at given (time, weight)
            t_base: Base time in seconds
            dt: Delta time in seconds
            chart_speed: Chart playback speed multiplier
            samples: Number of samples for motion blur (1 = no blur)
            shutter: Shutter angle (0.0-2.0, 0 = no blur)
        """
        # Clamp parameters
        samples = max(1, int(samples))
        shutter = clamp(float(shutter), 0.0, 2.0)

        # No motion blur
        if samples <= 1 or shutter <= 1e-6:
            render_callback(t_base, 1.0)
            return

        # Setup additive blending
        self.begin_blur()

        # Calculate weight per sample
        w = 1.0 / float(samples)
        dt_chart = float(dt) * chart_speed

        # Render multiple samples
        for i in range(samples):
            # Calculate time offset for this sample
            frac = 0.0 if samples <= 1 else (float(i) / float(samples - 1))
            t_s = t_base - shutter * dt_chart * (1.0 - frac)

            # Render scene at this time with reduced weight
            render_callback(t_s, w)

        # Restore blend mode
        self.end_blur()


def get_motion_blur_config(state: Any) -> tuple[int, float]:
    """
    Get motion blur configuration from state.

    Args:
        state: State object with motion blur settings

    Returns:
        Tuple of (samples, shutter)
    """
    samples = 1
    shutter = 0.0

    if getattr(state, "motion_blur_samples", None) is not None:
        try:
            samples = max(1, int(getattr(state, "motion_blur_samples")))
        except:
            samples = 1

    if getattr(state, "motion_blur_shutter", None) is not None:
        try:
            shutter = clamp(float(getattr(state, "motion_blur_shutter")), 0.0, 2.0)
        except:
            shutter = 0.0

    return samples, shutter
