"""Backward compatibility module for runtime -> engine migration.

DEPRECATED: This module is deprecated. Use phic_renderer.engine instead.
All functionality has been moved to the engine module.
"""

import warnings

warnings.warn(
    "phic_renderer.runtime is deprecated. Use phic_renderer.engine instead.",
    DeprecationWarning,
    stacklevel=2
)

# Re-export everything from engine for backward compatibility
from ..engine import *  # noqa: F401, F403
from ..engine import (
    # Core modules
    advance,
    effects,
    judge,
    judge_script,
    kinematics,
    timewarp,
    visibility,
    # New modules
    hold_system,
    note_manager,
    chart_init,
    judgment_helpers,
    miss_detection,
    manual_judgment,
    simulateplay,
    # Mods
    mods,
)

__all__ = [
    "advance",
    "effects",
    "judge",
    "judge_script",
    "kinematics",
    "timewarp",
    "visibility",
    "hold_system",
    "note_manager",
    "chart_init",
    "judgment_helpers",
    "miss_detection",
    "manual_judgment",
    "simulateplay",
    "mods",
]
