"""Backward compatibility module for io -> assets migration.

DEPRECATED: This module is deprecated. Use phic_renderer.assets instead.
All functionality has been moved to the assets module.
"""

import warnings

warnings.warn(
    "phic_renderer.io is deprecated. Use phic_renderer.assets instead.",
    DeprecationWarning,
    stacklevel=2
)

# Re-export everything from assets for backward compatibility
from ..assets import *  # noqa: F401, F403
from ..assets import (
    loader,
    chartpack,
    respack,
    background,
    fonts,
)

__all__ = [
    "loader",
    "chartpack",
    "respack",
    "background",
    "fonts",
]
