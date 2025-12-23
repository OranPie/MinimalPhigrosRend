"""ModernGL rendering backend.

This module provides the ModernGL-based rendering implementation.
"""

from __future__ import annotations
from typing import Any

# Re-export the run function from backend.py
from .backend import run

__all__ = ["run"]
