from __future__ import annotations

from typing import Any

def run(args: Any, **ctx: Any):
    from ..backends.moderngl import run as _run

    return _run(args, **ctx)
