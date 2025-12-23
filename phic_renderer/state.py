"""Global mutable state shared across modules.

DEPRECATION WARNING:
This module exists to preserve compatibility with the original single-file renderer,
while allowing multi-module splitting.

⚠️  This global state pattern is being phased out in favor of explicit
    configuration (RenderConfig) and session-scoped resources (ResourceContext).

    New code should use:
    - phic_renderer.config.RenderConfig for configuration
    - phic_renderer.core.ResourceContext for resources
    - phic_renderer.engine modules with explicit parameters

    See the refactoring plan for migration details.
"""

from __future__ import annotations
from typing import Optional, Any, Dict
import warnings


# Issue deprecation warning on import
warnings.warn(
    "The 'state' module is deprecated and will be removed in a future version. "
    "Use RenderConfig and ResourceContext instead. "
    "See the refactoring plan for migration details.",
    DeprecationWarning,
    stacklevel=2
)

# Loaded respack instance (see phic_renderer.respack.Respack)
respack: Optional[Any] = None

# Expand factor used by visibility / camera mapping.
expand_factor: float = 1.0

note_speed_mul_affects_travel: bool = False

note_flow_speed_multiplier: float = 1.0

note_scale_x: float = 1.0

note_scale_y: float = 1.0

force_line_alpha01: Optional[float] = None

force_line_alpha01_by_lid: Optional[Dict[int, float]] = None

render_overrender: Optional[float] = None

trail_alpha: Optional[float] = None
trail_frames: Optional[int] = None
trail_decay: Optional[float] = None
trail_blur: Optional[int] = None
trail_dim: Optional[int] = None
trail_blur_ramp: Optional[bool] = None
trail_blend: Optional[str] = None

motion_blur_samples: Optional[int] = None
motion_blur_shutter: Optional[float] = None
