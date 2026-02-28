"""Optional GUI launcher package.

This package is intentionally optional: importing it requires PySide6 or PyQt6.
"""

from __future__ import annotations

__all__ = ["run_gui"]

from .qt_launcher import run_gui
