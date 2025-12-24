"""Backward compatibility module for renderer -> backends migration.

DEPRECATED: This module is deprecated. Use phic_renderer.backends instead.
All functionality has been moved to the backends module.
"""

import warnings

warnings.warn(
    "phic_renderer.renderer is deprecated. Use phic_renderer.backends instead.",
    DeprecationWarning,
    stacklevel=2
)

# Re-export main run function for backward compatibility
from ..backends import run  # noqa: F401

__all__ = ["run"]
