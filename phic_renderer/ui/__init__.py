"""UI utilities module.

Provides backend-agnostic UI utilities for scoring, formatting, and internationalization.
"""

from .scoring import compute_score, format_title, progress_ratio

__all__ = ["compute_score", "format_title", "progress_ratio"]
