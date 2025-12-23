"""Rendering backend dispatcher.

Routes to appropriate backend (Pygame, ModernGL) based on configuration.
"""

from __future__ import annotations
from typing import Any


def run(args: Any, **ctx: Any) -> Any:
    """Run rendering with appropriate backend.

    Args:
        args: Argument namespace with backend selection
        **ctx: Additional context (backend override, etc.)

    Returns:
        Backend-specific result

    Raises:
        SystemExit: If backend is unknown
    """
    backend = getattr(args, "backend", None) or ctx.get("backend") or "pygame"
    backend = str(backend).strip().lower()

    if backend == "pygame":
        from .pygame import run as _run
        return _run(args, **ctx)

    if backend in {"moderngl", "gl", "opengl"}:
        from .moderngl import run as _run
        return _run(args, **ctx)

    raise SystemExit(f"Unknown backend: {backend}")


__all__ = ["run"]
