from __future__ import annotations

from typing import Any


def create_context() -> Any:
    try:
        import moderngl  # type: ignore
    except:
        raise SystemExit(
            "ModernGL backend requires the 'moderngl' package. Install it to use --backend moderngl."
        )

    last_err: Exception | None = None
    for req in (330, 320, 310, 300):
        try:
            return moderngl.create_context(require=req)
        except Exception as e:
            last_err = e

    msg = str(last_err) if last_err is not None else "failed to create OpenGL context"
    if "got version 0" in msg.lower() or "version 0" in msg.lower():
        raise SystemExit(
            "Failed to create a valid OpenGL context (got version 0). "
            "This usually means the OpenGL context was not created correctly (headless/remote session), "
            "or your environment does not provide OpenGL 3.x. Try --backend pygame."
        )
    raise SystemExit(f"Failed to create ModernGL context: {msg}")
