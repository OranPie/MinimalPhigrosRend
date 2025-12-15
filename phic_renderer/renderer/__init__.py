from __future__ import annotations

from typing import Any, Dict


def run(args: Any, **ctx: Any):
    backend = getattr(args, "backend", None) or ctx.get("backend") or "pygame"
    backend = str(backend).strip().lower()

    if backend == "pygame":
        from .pygame_backend import run as _run
        return _run(args, **ctx)

    if backend in {"moderngl", "gl", "opengl"}:
        from .moderngl_backend import run as _run
        return _run(args, **ctx)

    raise SystemExit(f"Unknown backend: {backend}")
