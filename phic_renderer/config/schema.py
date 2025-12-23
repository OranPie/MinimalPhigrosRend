"""Immutable rendering configuration.

This module replaces the global mutable state pattern (state.py) with an
immutable configuration object that is passed explicitly through the call chain.
"""

from __future__ import annotations
from dataclasses import dataclass, field
from typing import Optional, Dict, Any


@dataclass(frozen=True)
class RenderConfig:
    """Immutable rendering configuration.

    Replaces global state variables from state.py with explicit configuration
    that is passed through function calls, enabling better testability and
    concurrent session support.
    """

    # Visual settings
    expand_factor: float = 1.0
    note_scale_x: float = 1.0
    note_scale_y: float = 1.0
    note_flow_speed_multiplier: float = 1.0
    note_speed_mul_affects_travel: bool = False

    # Line settings
    force_line_alpha01: Optional[float] = None
    force_line_alpha01_by_lid: Optional[Dict[int, float]] = None

    # Rendering settings
    render_overrender: Optional[float] = None

    # Trail effect settings
    trail_alpha: Optional[float] = None
    trail_frames: Optional[int] = None
    trail_decay: Optional[float] = None
    trail_blur: Optional[int] = None
    trail_dim: Optional[int] = None
    trail_blur_ramp: Optional[bool] = None
    trail_blend: Optional[str] = None

    # Motion blur settings
    motion_blur_samples: Optional[int] = None
    motion_blur_shutter: Optional[float] = None

    @classmethod
    def from_state_module(cls, state: Any) -> RenderConfig:
        """Create RenderConfig from legacy state module.

        This is a compatibility helper for migrating from global state.

        Args:
            state: The state module with global variables

        Returns:
            RenderConfig instance with values from state
        """
        return cls(
            expand_factor=state.expand_factor,
            note_scale_x=state.note_scale_x,
            note_scale_y=state.note_scale_y,
            note_flow_speed_multiplier=state.note_flow_speed_multiplier,
            note_speed_mul_affects_travel=state.note_speed_mul_affects_travel,
            force_line_alpha01=state.force_line_alpha01,
            force_line_alpha01_by_lid=state.force_line_alpha01_by_lid,
            render_overrender=state.render_overrender,
            trail_alpha=state.trail_alpha,
            trail_frames=state.trail_frames,
            trail_decay=state.trail_decay,
            trail_blur=state.trail_blur,
            trail_dim=state.trail_dim,
            trail_blur_ramp=state.trail_blur_ramp,
            trail_blend=state.trail_blend,
            motion_blur_samples=state.motion_blur_samples,
            motion_blur_shutter=state.motion_blur_shutter,
        )

    def to_state_module(self, state: Any) -> None:
        """Update legacy state module with config values.

        This is a compatibility helper for backward compatibility during migration.

        Args:
            state: The state module to update
        """
        state.expand_factor = self.expand_factor
        state.note_scale_x = self.note_scale_x
        state.note_scale_y = self.note_scale_y
        state.note_flow_speed_multiplier = self.note_flow_speed_multiplier
        state.note_speed_mul_affects_travel = self.note_speed_mul_affects_travel
        state.force_line_alpha01 = self.force_line_alpha01
        state.force_line_alpha01_by_lid = self.force_line_alpha01_by_lid
        state.render_overrender = self.render_overrender
        state.trail_alpha = self.trail_alpha
        state.trail_frames = self.trail_frames
        state.trail_decay = self.trail_decay
        state.trail_blur = self.trail_blur
        state.trail_dim = self.trail_dim
        state.trail_blur_ramp = self.trail_blur_ramp
        state.trail_blend = self.trail_blend
        state.motion_blur_samples = self.motion_blur_samples
        state.motion_blur_shutter = self.motion_blur_shutter
