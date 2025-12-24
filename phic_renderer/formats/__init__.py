"""Backward compatibility module for formats -> chart migration.

DEPRECATED: This module is deprecated. Use phic_renderer.chart instead.
All functionality has been moved to the chart module.
"""

import warnings

warnings.warn(
    "phic_renderer.formats is deprecated. Use phic_renderer.chart instead.",
    DeprecationWarning,
    stacklevel=2
)

# Re-export everything from chart for backward compatibility
from ..chart import *  # noqa: F401, F403
from ..chart import (
    loader,
    official,
    rpe,
    pec,
)

__all__ = [
    "loader",
    "official",
    "rpe",
    "pec",
]
