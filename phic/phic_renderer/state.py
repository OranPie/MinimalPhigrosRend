"""Global mutable state shared across modules.

This module exists to preserve compatibility with the original single-file renderer,
while allowing multi-module splitting.
"""

from __future__ import annotations
from typing import Optional, Any

# Loaded respack instance (see phic_renderer.respack.Respack)
respack: Optional[Any] = None

# Expand factor used by visibility / camera mapping.
expand_factor: float = 1.0
